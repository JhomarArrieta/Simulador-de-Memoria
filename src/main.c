/*
 * main.c - ciclo principal del simulador de memoria virtual.
 *
 * Fase 8: la salida por defecto es exactamente el bloque de estadisticas que pide
 * el enunciado. La traza por comando y los tamanos de las estructuras quedan
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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    stats_t           stats;
    int               verboso;
    int               errores; /* comandos validos que no se pudieron ejecutar */
} simulador_t;

static int  parsear_opciones(int argc, char *argv[], opciones_t *op);
static void mostrar_uso(const char *programa);
static void mostrar_configuracion(const directorio_t *dir, const memoria_fisica_t *mem);
static int  procesar_archivo(simulador_t *sim, const char *ruta);
static void descartar_resto_de_linea(FILE *entrada);
static void ejecutar_comando(simulador_t *sim, long numero_linea, const comando_t *cmd);
static void ejecutar_alloc(simulador_t *sim, long numero_linea, const comando_t *cmd);
static void ejecutar_free(simulador_t *sim, long numero_linea, const comando_t *cmd);
static void ejecutar_acceso(simulador_t *sim, long numero_linea, const comando_t *cmd);
static void mostrar_detalle_final(const simulador_t *sim);

int main(int argc, char *argv[])
{
    simulador_t sim;
    opciones_t  op;
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

    /* La politica se prepara con el numero real de marcos. */
    if (!politica_init(op.politica, physmem_num_marcos(sim.mem))) {
        fprintf(stderr, "error: sin memoria para la politica de reemplazo\n");
        physmem_destruir(sim.mem);
        pagetable_destruir(sim.dir);
        return EXIT_FAILURE;
    }

    if (sim.verboso) {
        mostrar_configuracion(sim.dir, sim.mem);
    }

    lineas_malas = procesar_archivo(&sim, op.ruta);

    /* La salida que exige el enunciado; el resto es opcional. */
    stats_imprimir_reporte(&sim.stats, politica_nombre());
    if (sim.verboso) {
        mostrar_detalle_final(&sim);
    }

    /* Todo lo que se pidio con calloc se libera aqui: valgrind debe salir limpio
       incluso cuando la corrida termina en error. */
    politica_liberar();
    physmem_destruir(sim.mem);
    pagetable_destruir(sim.dir);

    if (lineas_malas < 0) {
        return EXIT_FAILURE;
    }
    if (lineas_malas > 0) {
        fprintf(stderr, "aviso: se ignoraron %d linea(s) mal formada(s)\n", lineas_malas);
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
    fprintf(stderr, "  -m  memoria fisica en KB (por defecto %d, minimo %d)\n",
            MEMORIA_FISICA_KB_DEFECTO, MEMORIA_FISICA_KB_MINIMA);
    fprintf(stderr, "  -v  traza cada comando y muestra el tamano de las estructuras\n");
    fprintf(stderr, "ejemplo: %s tests/t2_localidad.txt -p fifo -m 512\n", programa);
}

/*
 * Bloque provisional de la fase 2: deja ver que las estructuras se crearon con el
 * tamano esperado. Los sizeof salen del compilador, no de una cuenta a mano.
 */
static void mostrar_configuracion(const directorio_t *dir, const memoria_fisica_t *mem)
{
    unsigned long un_nivel = (unsigned long) PAGINAS_VIRTUALES * sizeof(pte_t);

    printf("=== configuracion ===\n");
    printf("tamano de pagina  : %u B\n", TAM_PAGINA);
    printf("espacio virtual   : 32 bits, %u paginas posibles\n", PAGINAS_VIRTUALES);
    printf("politica          : %s\n", politica_nombre());
    printf("memoria fisica    : %d KB, %d marcos (%d libres)\n",
           physmem_num_marcos(mem) * (int) (TAM_PAGINA / 1024),
           physmem_num_marcos(mem), physmem_num_libres(mem));
    printf("pte_t             : %zu B (pfn 20 b + valid + present + accessed + dirty)\n",
           sizeof(pte_t));
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
 * Lee el archivo completo y muestra cada comando parseado.
 * Devuelve cuantas lineas mal formadas se ignoraron, o -1 si el archivo no se
 * pudo abrir. Una linea invalida no aborta la corrida: se avisa y se sigue, para
 * que un typo en la linea 300 de una prueba no tire las 299 anteriores.
 */
static int procesar_archivo(simulador_t *sim, const char *ruta)
{
    FILE *entrada;
    char  linea[MAX_LINEA];
    long  numero_linea = 0;
    int   lineas_malas = 0;

    entrada = fopen(ruta, "r");
    if (entrada == NULL) {
        fprintf(stderr, "error: no se pudo abrir '%s': %s\n", ruta, strerror(errno));
        return -1;
    }

    while (fgets(linea, (int) sizeof linea, entrada) != NULL) {
        comando_t cmd;

        numero_linea++;

        /* Sin '\n' y sin haber llegado al final del archivo, la linea no cabio en
           el buffer. Se descarta entera para no partirla en dos comandos falsos. */
        if (strchr(linea, '\n') == NULL && !feof(entrada)) {
            fprintf(stderr, "[linea %ld] ignorada: la linea excede %d caracteres\n",
                    numero_linea, MAX_LINEA - 2);
            descartar_resto_de_linea(entrada);
            lineas_malas++;
            continue;
        }

        parser_parsear_linea(linea, &cmd);

        switch (cmd.tipo) {
        case CMD_IGNORAR:
            break; /* linea vacia o comentario */
        case CMD_ERROR:
            fprintf(stderr, "[linea %ld] ignorada: %s\n", numero_linea, cmd.motivo);
            lineas_malas++;
            break;
        default:
            ejecutar_comando(sim, numero_linea, &cmd);
            break;
        }
    }

    fclose(entrada);
    return lineas_malas;
}

/* Consume lo que quede de una linea que no cupo en el buffer. */
static void descartar_resto_de_linea(FILE *entrada)
{
    int c;

    while ((c = fgetc(entrada)) != '\n' && c != EOF) {
        /* nada que hacer: solo avanzar */
    }
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
    case CMD_IGNORAR:
    case CMD_ERROR:
        break; /* no llegan aqui */
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

    switch (pagetable_free(sim->dir, sim->mem, cmd->direccion, &paginas, &marcos)) {
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
    int      es_escritura = (cmd->tipo == CMD_WRITE);
    acceso_t acc;

    if (es_escritura) {
        unsigned char valor;

        /* La memoria guarda un byte por direccion, asi que un valor mas grande no
           cabe. Se avisa y se trunca en vez de descartar el acceso. */
        if (cmd->valor > 0xFFu) {
            fprintf(stderr, "[linea %ld] aviso: el valor %" PRIu32
                    " no cabe en un byte, se guarda %" PRIu32 "\n",
                    numero_linea, cmd->valor, cmd->valor & 0xFFu);
        }
        valor = (unsigned char) (cmd->valor & 0xFFu);
        acc   = acceso_escribir(sim->dir, sim->mem, &sim->stats, cmd->direccion, valor);
    } else {
        acc = acceso_leer(sim->dir, sim->mem, &sim->stats, cmd->direccion);
    }

    switch (acc.resultado) {
    case ACCESO_HIT:
    case ACCESO_FALLO_RESUELTO:
        if (!sim->verboso) {
            break;
        }
        printf("[linea %3ld] %-5s 0x%08" PRIX32 " = %-3u -> %-5s marco %2d, PA 0x%08" PRIX32 "\n",
               numero_linea, es_escritura ? "write" : "read", cmd->direccion, acc.valor,
               acceso_nombre_resultado(acc.resultado), acc.marco, acc.pa);
        break;
    case ACCESO_ILEGAL:
        /* Falla el programa simulado, no el simulador: se cuenta en las
           estadisticas como acceso ilegal y no afecta el codigo de salida. */
        fprintf(stderr, "[linea %ld] acceso ilegal: %s 0x%08" PRIX32
                " (valid=0, esa pagina no fue asignada)\n",
                numero_linea, es_escritura ? "write" : "read", cmd->direccion);
        break;
    case ACCESO_SIN_MARCOS:
        /* Con la politica de reemplazo activa esto ya no deberia ocurrir nunca:
           si ocurre, es un error interno del simulador. */
        fprintf(stderr, "[linea %ld] error interno: %s 0x%08" PRIX32
                " no consiguio marco ni despues de desalojar\n",
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
    stats_imprimir_detalle(&sim->stats);
}
