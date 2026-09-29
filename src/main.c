/*
 * main.c - ciclo principal del simulador de memoria virtual.
 *
 * Fase 2: ya existen la tabla de paginas de dos niveles y la memoria fisica, se
 * crean al arrancar y se destruyen al salir. Los comandos todavia no se ejecutan:
 * se parsean y se imprimen. La traduccion VA->PA llega en la fase 3.
 */
#include "config.h"
#include "pagetable.h"
#include "parser.h"
#include "physmem.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void mostrar_uso(const char *programa);
static void mostrar_configuracion(const directorio_t *dir, const memoria_fisica_t *mem);
static int  procesar_archivo(const char *ruta);
static void descartar_resto_de_linea(FILE *entrada);
static void mostrar_comando(long numero_linea, const comando_t *cmd);

int main(int argc, char *argv[])
{
    directorio_t     *dir;
    memoria_fisica_t *mem;
    int               lineas_malas;

    /* Los flags -p (politica) y -m (memoria fisica) llegan en una fase posterior;
       por ahora el unico argumento es la ruta del archivo de entrada. */
    if (argc != 2) {
        mostrar_uso(argv[0]);
        return EXIT_FAILURE;
    }

    dir = pagetable_crear();
    if (dir == NULL) {
        fprintf(stderr, "error: sin memoria para el directorio de paginas\n");
        return EXIT_FAILURE;
    }

    mem = physmem_crear(MEMORIA_FISICA_KB_DEFECTO);
    if (mem == NULL) {
        fprintf(stderr, "error: no se pudo crear la memoria fisica\n");
        pagetable_destruir(dir);
        return EXIT_FAILURE;
    }

    mostrar_configuracion(dir, mem);
    lineas_malas = procesar_archivo(argv[1]);

    /* Todo lo que se pidio con calloc se libera aqui: valgrind debe salir limpio
       incluso cuando la corrida termina en error. */
    physmem_destruir(mem);
    pagetable_destruir(dir);

    if (lineas_malas < 0) {
        return EXIT_FAILURE;
    }
    if (lineas_malas > 0) {
        fprintf(stderr, "aviso: se ignoraron %d linea(s) mal formada(s)\n", lineas_malas);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void mostrar_uso(const char *programa)
{
    fprintf(stderr, "uso: %s <archivo_entrada>\n", programa);
    fprintf(stderr, "ejemplo: %s tests/t1_basico.txt\n", programa);
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
static int procesar_archivo(const char *ruta)
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
            mostrar_comando(numero_linea, &cmd);
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

/*
 * Impresion provisional de la fase 1: sirve para verificar a ojo que los campos
 * se extrajeron bien. Desaparece cuando el comando se ejecute de verdad.
 * Se usan las macros de <inttypes.h> (PRIu32, PRIX32) porque el tipo exacto de
 * uint32_t depende de la plataforma y con -Werror un formato que no calce
 * rompe la compilacion.
 */
static void mostrar_comando(long numero_linea, const comando_t *cmd)
{
    const char *nombre = parser_nombre_comando(cmd->tipo);

    switch (cmd->tipo) {
    case CMD_ALLOC:
        printf("[linea %3ld] %-5s bytes=%" PRIu32 "\n",
               numero_linea, nombre, cmd->bytes);
        break;
    case CMD_WRITE:
        printf("[linea %3ld] %-5s dir=0x%08" PRIX32 " valor=%" PRIu32 "\n",
               numero_linea, nombre, cmd->direccion, cmd->valor);
        break;
    case CMD_READ:
    case CMD_FREE:
        printf("[linea %3ld] %-5s dir=0x%08" PRIX32 "\n",
               numero_linea, nombre, cmd->direccion);
        break;
    case CMD_IGNORAR:
    case CMD_ERROR:
        break; /* estos casos no llegan aqui */
    }
}
