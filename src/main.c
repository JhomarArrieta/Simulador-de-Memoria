/*
 * main.c - ciclo principal del simulador de memoria virtual.
 *
 * La salida por defecto es exactamente el bloque de estadisticas que pide
 * el enunciado, mas las lineas de tiempo. La traza por comando y los tamanos de las estructuras quedan
 * detras de -v, para que la salida especificada no se pierda entre cientos de
 * lineas de traza.
 */
#include "config.h"
#include "fallos.h"
#include "pagetable.h"
#include "parser.h"
#include "physmem.h"
#include "replace.h"
#include "stats.h"
#include "swap.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Estado del simulador. Se pasa por parametro en vez de usar variables globales:
 * asi cada funcion declara de que depende y se puede probar aparte.
 */
/* Opciones de la linea de comandos. */
typedef struct {
    const char *ruta;
    politica_t  politica;
    uint32_t    memoria_kb;
    int         verboso; /* -v: traza por comando y tamanos de las estructuras */
} opciones_t;

typedef struct {
    directorio_t     *dir;
    memoria_fisica_t *mem;
    area_swap_t      *swap;
    stats_t           stats;
    int               verboso;
    int               errores; /* comandos validos que no se pudieron ejecutar */
} simulador_t;

static int  parsear_opciones(int argc, char *argv[], opciones_t *op);
static void mostrar_uso(const char *programa);
static void mostrar_configuracion(const directorio_t *dir, const memoria_fisica_t *mem);
static int  procesar_archivo(simulador_t *sim, const char *ruta);
static void ejecutar_comando(simulador_t *sim, long numero_linea, const comando_t *cmd);
static void ejecutar_alloc(simulador_t *sim, long numero_linea, const comando_t *cmd);
static void ejecutar_free(simulador_t *sim, long numero_linea, const comando_t *cmd);
static void ejecutar_acceso(simulador_t *sim, long numero_linea, const comando_t *cmd);
static void mostrar_detalle_final(const simulador_t *sim);

int main(int argc, char *argv[])
{
    simulador_t sim;
    opciones_t  op;
    clock_t     inicio;
    int         lineas_malas;

    if (!parsear_opciones(argc, argv, &op)) {
        mostrar_uso(argv[0]);
        return EXIT_FAILURE;
    }

    sim.errores = 0;
    sim.verboso = op.verboso;
    stats_init(&sim.stats);
    sim.dir = pagetable_crear();
    if (sim.dir == NULL) {
        fprintf(stderr, "error: sin memoria para el directorio de paginas\n");
        return EXIT_FAILURE;
    }

    sim.mem = physmem_crear(op.memoria_kb);
    if (sim.mem == NULL) {
        fprintf(stderr, "error: no se pudo crear una memoria fisica de %" PRIu32 " KB\n",
                op.memoria_kb);
        pagetable_destruir(sim.dir);
        return EXIT_FAILURE;
    }

    sim.swap = swap_crear();
    if (sim.swap == NULL) {
        fprintf(stderr, "error: sin memoria para el area de swap\n");
        physmem_destruir(sim.mem);
        pagetable_destruir(sim.dir);
        return EXIT_FAILURE;
    }

    /* La politica se prepara con el numero real de marcos. */
    if (!politica_init(op.politica, physmem_num_marcos(sim.mem))) {
        fprintf(stderr, "error: sin memoria para la politica de reemplazo\n");
        swap_destruir(sim.swap);
        physmem_destruir(sim.mem);
        pagetable_destruir(sim.dir);
        return EXIT_FAILURE;
    }

    if (sim.verboso) {
        mostrar_configuracion(sim.dir, sim.mem);
    }

    /* clock() es C89/C99 estandar, a diferencia de gettimeofday, que es POSIX y
       con -std=c99 estricto no estaria declarado. */
    inicio       = clock();
    lineas_malas = procesar_archivo(&sim, op.ruta);
    stats_registrar_tiempo_cpu(&sim.stats,
                               (double) (clock() - inicio) / (double) CLOCKS_PER_SEC);

    /* La salida que exige el enunciado; el resto es opcional. */
    stats_imprimir_reporte(&sim.stats, politica_nombre());
    if (sim.verboso) {
        mostrar_detalle_final(&sim);
    }

    /* Todo lo que se pidio con calloc se libera aqui: valgrind debe salir limpio
       incluso cuando la corrida termina en error. */
    politica_liberar();
    swap_destruir(sim.swap);
    physmem_destruir(sim.mem);
    pagetable_destruir(sim.dir);

    if (lineas_malas < 0) {
        return EXIT_FAILURE;
    }
    if (lineas_malas > 0) {
        fprintf(stderr, "aviso: se ignoraron %d comando(s) mal formado(s)\n", lineas_malas);
    }
    if (sim.errores > 0) {
        fprintf(stderr, "aviso: %d comando(s) no se pudieron ejecutar\n", sim.errores);
    }

    return (lineas_malas > 0 || sim.errores > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}

/*
 * Parsea la linea de comandos: un archivo de entrada obligatorio y los flags
 * opcionales -p y -m, en cualquier orden. Devuelve 0 si algo esta mal, tras
 * explicar que fue. No se usa getopt porque con -std=c99 estricto glibc no lo
 * declara y -Werror convertiria eso en un error de compilacion.
 */
static int parsear_opciones(int argc, char *argv[], opciones_t *op)
{
    int i;

    op->ruta       = NULL;
    op->politica   = POLITICA_LRU;               /* el defecto del laboratorio */
    op->memoria_kb = MEMORIA_FISICA_KB_DEFECTO;  /* 256 KB */
    op->verboso    = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: -p necesita un valor (lru o fifo)\n");
                return 0;
            }
            if (strcmp(argv[i], "lru") == 0) {
                op->politica = POLITICA_LRU;
            } else if (strcmp(argv[i], "fifo") == 0) {
                op->politica = POLITICA_FIFO;
            } else {
                fprintf(stderr, "error: politica '%s' desconocida, use lru o fifo\n", argv[i]);
                return 0;
            }
        } else if (strcmp(argv[i], "-m") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: -m necesita el tamano en KB\n");
                return 0;
            }
            if (!parser_leer_uint32(argv[i], &op->memoria_kb)) {
                fprintf(stderr, "error: '%s' no es un tamano valido en KB\n", argv[i]);
                return 0;
            }
            if (op->memoria_kb < MEMORIA_FISICA_KB_MINIMA) {
                fprintf(stderr, "error: la memoria fisica minima del enunciado es %d KB\n",
                        MEMORIA_FISICA_KB_MINIMA);
                return 0;
            }
            if (op->memoria_kb > MEMORIA_FISICA_KB_MAXIMA) {
                fprintf(stderr, "error: la memoria fisica maxima del simulador es %u KB\n",
                        MEMORIA_FISICA_KB_MAXIMA);
                return 0;
            }
            if (op->memoria_kb % (TAM_PAGINA / 1024u) != 0) {
                fprintf(stderr, "error: la memoria fisica debe ser multiplo del tamano de "
                        "pagina (%u KB)\n", TAM_PAGINA / 1024u);
                return 0;
            }
        } else if (strcmp(argv[i], "-v") == 0) {
            op->verboso = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: opcion '%s' desconocida\n", argv[i]);
            return 0;
        } else if (op->ruta != NULL) {
            fprintf(stderr, "error: solo se acepta un archivo de entrada\n");
            return 0;
        } else {
            op->ruta = argv[i];
        }
    }

    if (op->ruta == NULL) {
        fprintf(stderr, "error: falta el archivo de entrada\n");
        return 0;
    }
    return 1;
}

static void mostrar_uso(const char *programa)
{
    fprintf(stderr, "uso: %s <archivo_entrada> [-p lru|fifo] [-m <KB_memoria_fisica>]\n",
            programa);
    fprintf(stderr, "  -p  politica de reemplazo (por defecto lru)\n");
    fprintf(stderr, "  -m  memoria fisica en KB (por defecto %d, minimo %d, maximo %u)\n",
            MEMORIA_FISICA_KB_DEFECTO, MEMORIA_FISICA_KB_MINIMA, MEMORIA_FISICA_KB_MAXIMA);
    fprintf(stderr, "  el tamano de pagina se elige al compilar: make PAGE_BITS=<10..16> "
            "(actual: %u B)\n", TAM_PAGINA);
    fprintf(stderr, "  -v  traza cada comando y muestra el tamano de las estructuras\n");
    fprintf(stderr, "ejemplo: %s tests/t2_localidad.txt -p fifo -m 512\n", programa);
}

/*
 * Configuracion y tamano de las estructuras, solo con -v. Los sizeof salen del
 * compilador, no de una cuenta a mano.
 */
static void mostrar_configuracion(const directorio_t *dir, const memoria_fisica_t *mem)
{
    unsigned long un_nivel = (unsigned long) PAGINAS_VIRTUALES * sizeof(pte_t);

    printf("=== configuracion ===\n");
    printf("tamano de pagina  : %u B\n", TAM_PAGINA);
    printf("espacio virtual   : 32 bits = PT1 %d b | PT2 %d b | offset %d b, %u paginas posibles\n",
           BITS_NIVEL1, BITS_NIVEL2, BITS_OFFSET, PAGINAS_VIRTUALES);
    printf("politica          : %s\n", politica_nombre());
    printf("memoria fisica    : %d KB, %d marcos (%d libres)\n",
           physmem_num_marcos(mem) * (int) (TAM_PAGINA / 1024),
           physmem_num_marcos(mem), physmem_num_libres(mem));
    printf("pte_t             : %zu B (pfn %d b + valid + present + accessed + dirty + swapped)\n",
           sizeof(pte_t), BITS_PFN);
    printf("tabla_nivel2_t    : %zu B (%u entradas, cubre %lu B de espacio virtual)\n",
           sizeof(tabla_nivel2_t), ENTRADAS_NIVEL2, BYTES_POR_TABLA_NIVEL2);
    printf("directorio_t      : %zu B (%u punteros), tablas de nivel 2 vivas: %zu\n",
           sizeof(directorio_t), ENTRADAS_NIVEL1, pagetable_tablas_nivel2(dir));
    printf("marco_t           : %zu B por marco de metadatos\n", sizeof(marco_t));
    printf("traduccion ahora  : %zu B  (una tabla de un solo nivel costaria %lu B)\n",
           pagetable_memoria_usada(dir), un_nivel);
    printf("memoria fisica    : %zu B (datos + metadatos)\n", physmem_memoria_usada(mem));
    printf("=== comandos ===\n");
}

/*
 * Lee el archivo completo y ejecuta cada comando.
 * Devuelve cuantos comandos mal formados se ignoraron, o -1 si el archivo no se
 * pudo abrir. Un comando invalido no aborta la corrida: se avisa y se sigue, para
 * que un typo en la linea 300 de una prueba no tire las 299 anteriores.
 */
static int procesar_archivo(simulador_t *sim, const char *ruta)
{
    FILE     *entrada;
    lector_t  lector;
    comando_t cmd;
    int       comandos_malos = 0;

    entrada = fopen(ruta, "r");
    if (entrada == NULL) {
        fprintf(stderr, "error: no se pudo abrir '%s': %s\n", ruta, strerror(errno));
        return -1;
    }

    parser_iniciar(&lector, entrada);
    while (parser_siguiente_comando(&lector, &cmd)) {
        if (cmd.tipo == CMD_ERROR) {
            fprintf(stderr, "[linea %ld] comando ignorado: %s\n", cmd.linea, cmd.motivo);
            comandos_malos++;
            continue;
        }
        ejecutar_comando(sim, cmd.linea, &cmd);
    }

    fclose(entrada);
    return comandos_malos;
}

/* Despacha un comando ya parseado. */
static void ejecutar_comando(simulador_t *sim, long numero_linea, const comando_t *cmd)
{
    switch (cmd->tipo) {
    case CMD_ALLOC:
        ejecutar_alloc(sim, numero_linea, cmd);
        break;
    case CMD_FREE:
        ejecutar_free(sim, numero_linea, cmd);
        break;
    case CMD_READ:
    case CMD_WRITE:
        ejecutar_acceso(sim, numero_linea, cmd);
        break;
    case CMD_ERROR:
        break; /* procesar_archivo los filtra antes */
    }
}

static void ejecutar_alloc(simulador_t *sim, long numero_linea, const comando_t *cmd)
{
    uint32_t va_inicio;
    uint32_t paginas;

    switch (pagetable_alloc(sim->dir, cmd->bytes, &va_inicio, &paginas)) {
    case ALLOC_OK:
        if (!sim->verboso) {
            break;
        }
        printf("[linea %3ld] alloc %" PRIu32 " B -> %" PRIu32
               " pagina(s), VA 0x%08" PRIX32 "-0x%08" PRIX32 ", tablas nivel 2: %zu\n",
               numero_linea, cmd->bytes, paginas, va_inicio,
               va_inicio + paginas * TAM_PAGINA - 1,
               pagetable_tablas_nivel2(sim->dir));
        break;
    case ALLOC_CERO:
        fprintf(stderr, "[linea %ld] aviso: alloc de 0 bytes, no se asigno nada\n",
                numero_linea);
        break;
    case ALLOC_SIN_ESPACIO:
        fprintf(stderr, "[linea %ld] error: no queda espacio virtual para %" PRIu32 " B\n",
                numero_linea, cmd->bytes);
        sim->errores++;
        break;
    case ALLOC_SIN_MEMORIA:
        fprintf(stderr, "[linea %ld] error: sin memoria para las tablas de paginas\n",
                numero_linea);
        sim->errores++;
        break;
    }
}

static void ejecutar_free(simulador_t *sim, long numero_linea, const comando_t *cmd)
{
    uint32_t paginas;
    uint32_t marcos;

    switch (pagetable_free(sim->dir, sim->mem, sim->swap, cmd->direccion, &paginas, &marcos)) {
    case FREE_OK:
        if (!sim->verboso) {
            break;
        }
        printf("[linea %3ld] free  0x%08" PRIX32 " -> %" PRIu32 " pagina(s) invalidada(s), %"
               PRIu32 " marco(s) devuelto(s), tablas nivel 2: %zu\n",
               numero_linea, cmd->direccion, paginas, marcos,
               pagetable_tablas_nivel2(sim->dir));
        break;
    case FREE_NO_ES_INICIO:
        /* Como el free(ptr) de C con un puntero que no salio de malloc: no se
           libera nada. Se avisa y la corrida sigue. */
        fprintf(stderr, "[linea %ld] error: 0x%08" PRIX32
                " no es el inicio de ninguna asignacion viva\n",
                numero_linea, cmd->direccion);
        sim->errores++;
        break;
    }
}

/*
 * Ejecuta un read o un write: traduce, atiende el fallo si lo hay y toca la
 * memoria fisica. El valor que se imprime en un read es el que estaba en el
 * marco, asi se ve que el dato viajo por la traduccion correcta.
 */
static void ejecutar_acceso(simulador_t *sim, long numero_linea, const comando_t *cmd)
{
    int               es_escritura = (cmd->tipo == CMD_WRITE);
    sistema_memoria_t sis;
    acceso_t          acc;

    sis.dir  = sim->dir;
    sis.mem  = sim->mem;
    sis.swap = sim->swap;

    if (es_escritura) {
        acc = acceso_escribir(&sis, &sim->stats, cmd->direccion, cmd->valor);
    } else {
        acc = acceso_leer(&sis, &sim->stats, cmd->direccion);
    }

    switch (acc.resultado) {
    case ACCESO_HIT:
    case ACCESO_FALLO_RESUELTO:
        if (!sim->verboso) {
            break;
        }
        printf("[linea %3ld] %-5s 0x%08" PRIX32 " = %-10" PRIu32 " -> %-5s marco %2d, PA 0x%08" PRIX32,
               numero_linea, es_escritura ? "write" : "read", cmd->direccion, acc.valor,
               acceso_nombre_resultado(acc.resultado), acc.marco, acc.pa);
        if (acc.desde_swap) {
            printf(", traida del swap");
        }
        if (acc.vpn_victima >= 0) {
            printf(", desaloja la pagina 0x%05lX", (unsigned long) acc.vpn_victima);
        }
        printf("\n");
        break;
    case ACCESO_ILEGAL:
        /* Falla el programa simulado, no el simulador: se cuenta en las
           estadisticas como acceso ilegal y no afecta el codigo de salida. */
        fprintf(stderr, "[linea %ld] acceso ilegal: %s 0x%08" PRIX32
                " (valid=0, esa pagina no fue asignada)\n",
                numero_linea, es_escritura ? "write" : "read", cmd->direccion);
        break;
    case ACCESO_NO_ALINEADO:
        fprintf(stderr, "[linea %ld] acceso no alineado: %s 0x%08" PRIX32
                " (la direccion de una palabra de 32 bits debe ser multiplo de 4)\n",
                numero_linea, es_escritura ? "write" : "read", cmd->direccion);
        break;
    case ACCESO_ERROR_INTERNO:
        /* Con la politica de reemplazo activa esto solo ocurre si el simulador se
           queda sin memoria para el swap o si sus estructuras se desincronizan. */
        fprintf(stderr, "[linea %ld] error interno: %s 0x%08" PRIX32
                " no consiguio marco (sin memoria para el swap)\n",
                numero_linea, es_escritura ? "write" : "read", cmd->direccion);
        sim->errores++;
        break;
    }
}

/* Estado de las estructuras al final de la corrida. Solo en modo verboso. */
static void mostrar_detalle_final(const simulador_t *sim)
{
    printf("--- estructuras ---\n");
    printf("tablas de nivel 2 vivas      : %zu\n", pagetable_tablas_nivel2(sim->dir));
    printf("memoria de traduccion        : %zu B\n", pagetable_memoria_usada(sim->dir));
    printf("marcos ocupados              : %d de %d\n",
           physmem_num_marcos(sim->mem) - physmem_num_libres(sim->mem),
           physmem_num_marcos(sim->mem));
    printf("slots de swap en uso / pico  : %zu / %zu\n", swap_slots_en_uso(sim->swap),
           swap_pico_slots(sim->swap));
    stats_imprimir_detalle(&sim->stats);
}
