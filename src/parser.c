/*
 * parser.c - lector de tokens y conversion a comandos.
 */
#include "parser.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

void parser_iniciar(lector_t *lector, FILE *entrada)
{
    lector->entrada       = entrada;
    lector->linea         = 1;
    lector->hay_pendiente = 0;
}

/*
 * Lee el siguiente token del archivo. Devuelve 1 si hay token y 0 en EOF.
 * En *linea deja la linea donde empieza el token y en *largo un 1 si el token
 * no cupo en MAX_TOKEN (se trunca y el llamador lo reporta como invalido).
 */
static int leer_token(lector_t *lector, char *token, long *linea, int *largo)
{
    int    c;
    size_t n = 0;

    if (lector->hay_pendiente) {
        memcpy(token, lector->pendiente, MAX_TOKEN);
        *linea                = lector->linea_pendiente;
        *largo                = lector->pendiente_largo;
        lector->hay_pendiente = 0;
        return 1;
    }

    /* Saltar espacios y comentarios, contando los saltos de linea. */
    for (;;) {
        c = fgetc(lector->entrada);
        if (c == EOF) {
            return 0;
        }
        if (c == '\n') {
            lector->linea++;
        } else if (c == '#') {
            while ((c = fgetc(lector->entrada)) != '\n' && c != EOF) {
                /* el comentario llega hasta el fin de la linea */
            }
            if (c == EOF) {
                return 0;
            }
            lector->linea++;
        } else if (!isspace((unsigned char) c)) {
            break;
        }
    }

    *linea = lector->linea;
    *largo = 0;
    while (c != EOF && !isspace((unsigned char) c) && c != '#') {
        if (n < MAX_TOKEN - 1) {
            token[n++] = (char) c;
        } else {
            *largo = 1;
        }
        c = fgetc(lector->entrada);
    }
    token[n] = '\0';

    /* El caracter que corto el token se devuelve al flujo para que el '\n' se
       cuente y el '#' abra su comentario en la siguiente llamada. */
    if (c != EOF) {
        ungetc(c, lector->entrada);
    }
    return 1;
}

/* Devuelve un token al lector: la siguiente leer_token lo entrega de nuevo. */
static void devolver_token(lector_t *lector, const char *token, long linea, int largo)
{
    memcpy(lector->pendiente, token, MAX_TOKEN);
    lector->linea_pendiente = linea;
    lector->pendiente_largo = largo;
    lector->hay_pendiente   = 1;
}

/* Numero de argumentos del verbo, o -1 si el token no es un verbo. */
static int argumentos_de(const char *verbo, tipo_comando_t *tipo)
{
    if (strcmp(verbo, "alloc") == 0) {
        *tipo = CMD_ALLOC;
        return 1;
    }
    if (strcmp(verbo, "write") == 0) {
        *tipo = CMD_WRITE;
        return 2;
    }
    if (strcmp(verbo, "read") == 0) {
        *tipo = CMD_READ;
        return 1;
    }
    if (strcmp(verbo, "free") == 0) {
        *tipo = CMD_FREE;
        return 1;
    }
    return -1;
}

/*
 * Acepta decimal ("4096") y hexadecimal con prefijo explicito ("0x1000"). No se
 * usa strtoul con base 0 a proposito: alli un cero inicial activaria la lectura
 * octal y "08192" fallaria en el '8', un error silencioso y dificil de ver.
 */
int parser_leer_uint32(const char *token, uint32_t *destino)
{
    char         *fin;
    unsigned long valor;
    int           base = 10;

    if (token[0] == '\0') {
        return 0;
    }

    if (token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
        base = 16;
        token += 2;
        if (token[0] == '\0') {
            return 0; /* "0x" a secas no es un numero */
        }
    }

    /* strtoul acepta signos y espacios iniciales; los rechazamos a mano para que
       "write -4 1" no termine convertido en una direccion enorme. */
    if (base == 16 ? !isxdigit((unsigned char) token[0])
                   : !isdigit((unsigned char) token[0])) {
        return 0;
    }

    errno = 0;
    valor = strtoul(token, &fin, base);

    if (errno != 0 || *fin != '\0') {
        return 0; /* desbordamiento, o basura despues del numero */
    }
    if (valor > 0xFFFFFFFFUL) {
        return 0; /* no cabe en 32 bits */
    }

    *destino = (uint32_t) valor;
    return 1;
}

/* Marca el comando como invalido con un motivo fijo. */
static void marcar_error(comando_t *cmd, const char *motivo)
{
    cmd->tipo   = CMD_ERROR;
    cmd->motivo = motivo;
}

int parser_siguiente_comando(lector_t *lector, comando_t *cmd)
{
    char           verbo[MAX_TOKEN];
    char           argumento[MAX_TOKEN];
    uint32_t       numeros[2] = {0, 0};
    tipo_comando_t tipo;
    tipo_comando_t tipo_argumento;
    long           linea;
    int            largo;
    int            necesarios;
    int            i;

    cmd->direccion = 0;
    cmd->bytes     = 0;
    cmd->valor     = 0;
    cmd->motivo    = NULL;

    if (!leer_token(lector, verbo, &linea, &largo)) {
        return 0; /* fin del archivo */
    }
    cmd->linea = linea;

    necesarios = largo ? -1 : argumentos_de(verbo, &tipo);
    if (necesarios < 0) {
        marcar_error(cmd, "comando desconocido");
        return 1;
    }

    for (i = 0; i < necesarios; i++) {
        long linea_argumento;

        if (!leer_token(lector, argumento, &linea_argumento, &largo)) {
            marcar_error(cmd, "faltan argumentos al final del archivo");
            return 1;
        }
        /* Si en lugar del argumento llega otro verbo, el comando esta incompleto.
           El verbo se devuelve para que el siguiente comando no se pierda. */
        if (!largo && argumentos_de(argumento, &tipo_argumento) >= 0) {
            devolver_token(lector, argumento, linea_argumento, largo);
            marcar_error(cmd, "faltan argumentos");
            return 1;
        }
        if (largo || !parser_leer_uint32(argumento, &numeros[i])) {
            marcar_error(cmd, "argumento numerico invalido (decimal o 0x hexadecimal, 32 bits)");
            return 1;
        }
    }

    cmd->tipo = tipo;
    switch (tipo) {
    case CMD_ALLOC:
        cmd->bytes = numeros[0];
        break;
    case CMD_WRITE:
        cmd->direccion = numeros[0];
        cmd->valor     = numeros[1];
        break;
    case CMD_READ:
    case CMD_FREE:
        cmd->direccion = numeros[0];
        break;
    case CMD_ERROR:
        break; /* argumentos_de nunca devuelve este tipo */
    }
    return 1;
}
