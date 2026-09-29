/*
 * fallos.h - ejecucion de un acceso y manejo del fallo de pagina.
 *
 * Es la capa que en un SO real reparte el trabajo entre el hardware y el kernel:
 * la MMU traduce (mmu.c) y, si la pagina no esta presente, aqui se atiende el
 * fallo, se consigue un marco y se reintenta el acceso.
 */
#ifndef FALLOS_H
#define FALLOS_H

#include "pagetable.h"
#include "physmem.h"
#include "stats.h"

#include <stdint.h>

typedef enum {
    ACCESO_HIT,             /* la pagina ya estaba en memoria            */
    ACCESO_FALLO_RESUELTO,  /* hubo fallo y se consiguio un marco        */
    ACCESO_ILEGAL,          /* valid = 0: la pagina no fue asignada      */
    ACCESO_SIN_MARCOS       /* memoria llena y sin politica de reemplazo */
} resultado_acceso_t;

typedef struct {
    resultado_acceso_t resultado;
    uint32_t           pa;    /* direccion fisica usada, si el acceso se completo */
    int                marco; /* marco que quedo sirviendo la pagina, o -1        */
    unsigned char      valor; /* el leido en un read, el escrito en un write      */
} acceso_t;

/*
 * Ejecutan el acceso completo: traducen, atienden el fallo si lo hay, marcan
 * accessed (y dirty en la escritura) y tocan la memoria fisica. Actualizan los
 * contadores: todo acceso completado suma en accesos, y el fallo suma tambien en
 * fallos, de modo que accesos = hits + fallos siempre se cumple.
 */
acceso_t acceso_leer(directorio_t *dir, memoria_fisica_t *mem, stats_t *st, uint32_t va);
acceso_t acceso_escribir(directorio_t *dir, memoria_fisica_t *mem, stats_t *st,
                         uint32_t va, unsigned char valor);

const char *acceso_nombre_resultado(resultado_acceso_t resultado);

#endif /* FALLOS_H */
