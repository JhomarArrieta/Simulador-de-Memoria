/*
 * parser.h - lectura de los comandos del archivo de entrada.
 *
 * Esta capa se mantiene aparte del ciclo principal porque la rubrica pide
 * responsabilidades separadas: aqui no se sabe nada de paginas ni de marcos,
 * solo de sintaxis.
 *
 * El archivo se lee como un flujo de tokens separados por espacios, tabs o
 * saltos de linea. Asi se aceptan los dos formatos posibles del enunciado:
 *
 *   un comando por linea          todos en una linea (como aparece en el PDF)
 *   alloc 8192                    alloc 8192 write 0 42 write 4096 99 read 0
 *   write 0 42
 *   ...
 *
 * Un '#' inicia un comentario que termina en el salto de linea.
 */
#ifndef PARSER_H
#define PARSER_H

#include <stdint.h>
#include <stdio.h>

/* Longitud maxima de un token (verbo o numero), incluyendo el '\0'. */
#define MAX_TOKEN 64

typedef enum {
    CMD_ALLOC, /* alloc <bytes>                 */
    CMD_WRITE, /* write <virtual_addr> <value>  */
    CMD_READ,  /* read  <virtual_addr>          */
    CMD_FREE,  /* free  <virtual_addr>          */
    CMD_ERROR  /* comando mal formado           */
} tipo_comando_t;

typedef struct {
    tipo_comando_t tipo;
    uint32_t       direccion; /* write, read, free */
    uint32_t       bytes;     /* alloc             */
    uint32_t       valor;     /* write             */
    long           linea;     /* linea donde empieza el comando, para los mensajes */

    /*
     * Motivo del error cuando tipo == CMD_ERROR; NULL en cualquier otro caso.
     * Apunta siempre a un literal de cadena, nunca a memoria dinamica, para que
     * un comando_t se pueda copiar y descartar sin liberar nada.
     */
    const char    *motivo;
} comando_t;

/*
 * Estado del lector de tokens. Guarda un token de "vuelta atras": cuando a un
 * comando le falta un argumento y lo siguiente es otro verbo, ese verbo no se
 * consume, para que el error de un comando no arrastre al siguiente.
 */
typedef struct {
    FILE *entrada;
    long  linea;                    /* linea en la que esta el lector */
    int   hay_pendiente;            /* 1 si 'pendiente' guarda un token devuelto */
    char  pendiente[MAX_TOKEN];
    long  linea_pendiente;
    int   pendiente_largo;
} lector_t;

void parser_iniciar(lector_t *lector, FILE *entrada);

/*
 * Lee el siguiente comando del flujo. Devuelve 1 si leyo un comando (valido o
 * CMD_ERROR con su motivo) y 0 al llegar al final del archivo. Nunca aborta: el
 * que decide que hacer con un CMD_ERROR es quien llama.
 */
int parser_siguiente_comando(lector_t *lector, comando_t *cmd);

/*
 * Convierte un token en un entero sin signo de 32 bits: decimal, o hexadecimal
 * con prefijo 0x. Devuelve 1 si el token era un numero valido y completo, 0 si
 * no. Se expone porque los flags de la linea de comandos necesitan exactamente
 * la misma conversion, incluida la de no interpretar un cero inicial como octal.
 */
int parser_leer_uint32(const char *token, uint32_t *destino);

#endif /* PARSER_H */
