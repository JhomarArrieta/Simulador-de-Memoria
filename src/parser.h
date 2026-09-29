/*
 * parser.h - conversion de una linea de texto del archivo de entrada en un
 * comando que el simulador pueda ejecutar.
 *
 * Esta capa se mantiene aparte del ciclo principal porque la rubrica pide
 * responsabilidades separadas: aqui no se sabe nada de paginas ni de marcos,
 * solo de sintaxis.
 */
#ifndef PARSER_H
#define PARSER_H

#include <stdint.h>

/* Longitud maxima de una linea de entrada, incluyendo '\n' y '\0'. */
#define MAX_LINEA 256

typedef enum {
    CMD_ALLOC,   /* alloc <bytes>                 */
    CMD_WRITE,   /* write <virtual_addr> <value>  */
    CMD_READ,    /* read  <virtual_addr>          */
    CMD_FREE,    /* free  <virtual_addr>          */
    CMD_IGNORAR, /* linea vacia o comentario: no es un error */
    CMD_ERROR    /* linea mal formada                       */
} tipo_comando_t;

typedef struct {
    tipo_comando_t tipo;
    uint32_t       direccion; /* write, read, free */
    uint32_t       bytes;     /* alloc             */
    uint32_t       valor;     /* write             */

    /*
     * Motivo del error cuando tipo == CMD_ERROR; NULL en cualquier otro caso.
     * Apunta siempre a un literal de cadena, nunca a memoria dinamica, para que
     * un comando_t se pueda copiar y descartar sin liberar nada.
     */
    const char    *motivo;
} comando_t;

/*
 * Parsea una linea y deja el resultado en *cmd. El buffer 'linea' se modifica
 * (se tokeniza en el sitio), asi que no puede ser const ni un literal.
 * Nunca falla: el resultado de una linea invalida es un comando CMD_ERROR con
 * su motivo, y el que decide que hacer con eso es quien llama.
 */
void parser_parsear_linea(char *linea, comando_t *cmd);

/* Nombre del comando tal como aparece en el archivo de entrada. */
const char *parser_nombre_comando(tipo_comando_t tipo);

#endif /* PARSER_H */
