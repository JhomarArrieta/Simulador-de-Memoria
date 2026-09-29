/*
 * parser.c - implementacion del parseo de comandos.
 */
#include "parser.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Separadores de tokens. Incluimos '\r' y '\n' para que la linea que devuelve
   fgets no necesite limpiarse antes, y para tolerar archivos con finales de
   linea de Windows (CRLF). */
#define DELIMITADORES " \t\r\n"

/*
 * Siguiente token de la linea que se esta tokenizando, o NULL si ya no hay mas.
 * Un token que empieza con '#' se trata como inicio de comentario, de modo que
 * "read 0 # nota" es valido y el resto de la linea se descarta.
 */
static const char *siguiente_token(void)
{
    const char *token = strtok(NULL, DELIMITADORES);

    if (token != NULL && token[0] == '#') {
        return NULL;
    }
    return token;
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
        return 0; /* no cabe en una direccion virtual de 32 bits */
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

void parser_parsear_linea(char *linea, comando_t *cmd)
{
    const char *verbo;    /* punteros al propio buffer: se leen, nunca se escriben */
    const char *arg1;
    const char *arg2;
    const char *sobrante;

    /* Estado inicial limpio: quien lea el comando solo deberia mirar los campos
       que corresponden a su tipo, pero dejarlos en cero evita basura al depurar. */
    cmd->tipo      = CMD_IGNORAR;
    cmd->direccion = 0;
    cmd->bytes     = 0;
    cmd->valor     = 0;
    cmd->motivo    = NULL;

    verbo = strtok(linea, DELIMITADORES);
    if (verbo == NULL || verbo[0] == '#') {
        return; /* linea vacia o comentario: se ignora sin contarla como error */
    }

    /* Los argumentos se piden en cadena: si falta uno, no tiene sentido seguir
       tokenizando en busca de sobrantes. */
    arg1     = siguiente_token();
    arg2     = (arg1 != NULL) ? siguiente_token() : NULL;
    sobrante = (arg2 != NULL) ? siguiente_token() : NULL;

    if (strcmp(verbo, "alloc") == 0) {
        if (arg1 == NULL) {
            marcar_error(cmd, "alloc necesita el numero de bytes");
        } else if (arg2 != NULL) {
            marcar_error(cmd, "alloc recibe un solo argumento");
        } else if (!parser_leer_uint32(arg1, &cmd->bytes)) {
            marcar_error(cmd, "el numero de bytes de alloc no es valido");
        } else {
            cmd->tipo = CMD_ALLOC;
        }
    } else if (strcmp(verbo, "write") == 0) {
        if (arg1 == NULL || arg2 == NULL) {
            marcar_error(cmd, "write necesita direccion y valor");
        } else if (sobrante != NULL) {
            marcar_error(cmd, "write recibe exactamente dos argumentos");
        } else if (!parser_leer_uint32(arg1, &cmd->direccion)) {
            marcar_error(cmd, "la direccion de write no es valida");
        } else if (!parser_leer_uint32(arg2, &cmd->valor)) {
            marcar_error(cmd, "el valor de write no es valido");
        } else {
            cmd->tipo = CMD_WRITE;
        }
    } else if (strcmp(verbo, "read") == 0 || strcmp(verbo, "free") == 0) {
        if (arg1 == NULL) {
            marcar_error(cmd, "read y free necesitan una direccion");
        } else if (arg2 != NULL) {
            marcar_error(cmd, "read y free reciben un solo argumento");
        } else if (!parser_leer_uint32(arg1, &cmd->direccion)) {
            marcar_error(cmd, "la direccion no es valida");
        } else {
            cmd->tipo = (verbo[0] == 'r') ? CMD_READ : CMD_FREE;
        }
    } else {
        marcar_error(cmd, "comando desconocido");
    }
}
