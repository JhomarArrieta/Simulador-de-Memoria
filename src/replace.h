/*
 * replace.h - interfaz de la politica de reemplazo de paginas.
 *
 * El resto del simulador llama siempre a estas funciones y NUNCA pregunta que
 * politica esta activa. Cambiar de politica no debe tocar ni una linea fuera de
 * replace.c: si aparece un if (politica == ...) en otro archivo, esta mal.
 *
 * El estado vive como static dentro de replace.c porque la interfaz no lleva
 * parametro de contexto. Es estado privado del modulo, no una variable global del
 * programa: nadie de afuera puede verlo ni tocarlo. La unica limitacion que
 * impone es que no puede haber dos politicas vivas a la vez, y el simulador
 * nunca lo necesita.
 */
#ifndef REPLACE_H
#define REPLACE_H

typedef enum {
    POLITICA_LRU,
    POLITICA_FIFO
} politica_t;

/*
 * Prepara la politica para num_marcos marcos. Devuelve 0 si no hubo memoria.
 * (Unica diferencia con la interfaz de CLAUDE.md: devuelve int en vez de void,
 * para que un fallo de calloc no se trague en silencio ni obligue al modulo a
 * llamar a exit.)
 */
int politica_init(politica_t p, int num_marcos);

/* Una pagina acaba de entrar a este marco (despues de un fallo de pagina). */
void politica_al_cargar(int marco);

/* Se accedio a este marco. Se llama en CADA acceso, sea hit o fallo. */
void politica_al_acceder(int marco);

/*
 * Marco que hay que desalojar. Solo se llama cuando no quedan marcos libres, asi
 * que todos los marcos tienen una pagina.
 */
int politica_elegir_victima(void);

/* Libera el estado interno. Tolera que no se haya llamado a politica_init. */
void politica_liberar(void);

/* Nombre para la salida final. Asi main reporta la politica sin preguntar cual es. */
const char *politica_nombre(void);

#endif /* REPLACE_H */
