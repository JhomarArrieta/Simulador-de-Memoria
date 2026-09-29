/*
 * fallos.h - ejecucion de un acceso y manejo del fallo de pagina.
 *
 * Es la capa que en un SO real reparte el trabajo entre el hardware y el kernel:
 * la MMU traduce (mmu.c) y, si la pagina no esta presente, aqui se atiende el
 * fallo, se consigue un marco (libre o desalojando una victima), se trae la
 * pagina desde el swap si tiene copia, y se reintenta el acceso.
 */
#ifndef FALLOS_H
#define FALLOS_H

#include "pagetable.h"
#include "physmem.h"
#include "stats.h"
#include "swap.h"

#include <stdint.h>

typedef enum {
    ACCESO_HIT,            /* la pagina ya estaba en memoria                  */
    ACCESO_FALLO_RESUELTO, /* hubo fallo y se consiguio un marco              */
    ACCESO_ILEGAL,         /* valid = 0: la pagina no fue asignada            */
    ACCESO_NO_ALINEADO,    /* la direccion no es multiplo de TAM_PALABRA      */
    ACCESO_ERROR_INTERNO   /* sin marco o sin memoria para el swap: un bug o
                              un agotamiento de memoria del propio simulador */
} resultado_acceso_t;

/* Los tres componentes de la memoria que toca un acceso. */
typedef struct {
    directorio_t     *dir;
    memoria_fisica_t *mem;
    area_swap_t      *swap;
} sistema_memoria_t;

typedef struct {
    resultado_acceso_t resultado;
    uint32_t           pa;          /* direccion fisica usada, si el acceso se completo */
    int                marco;       /* marco que quedo sirviendo la pagina, o -1        */
    uint32_t           valor;       /* el leido en un read, el escrito en un write      */
    int                desde_swap;  /* 1 si el fallo trajo la pagina desde el swap      */
    long               vpn_victima; /* pagina desalojada para atender el fallo, o -1    */
} acceso_t;

/*
 * Ejecutan el acceso completo: traducen, atienden el fallo si lo hay, marcan
 * accessed (y dirty en la escritura) y tocan la memoria fisica. Actualizan los
 * contadores: todo acceso completado suma en accesos, y el fallo suma tambien en
 * fallos, de modo que accesos = hits + fallos siempre se cumple.
 */
acceso_t acceso_leer(sistema_memoria_t *sis, stats_t *st, uint32_t va);
acceso_t acceso_escribir(sistema_memoria_t *sis, stats_t *st, uint32_t va, uint32_t valor);

const char *acceso_nombre_resultado(resultado_acceso_t resultado);

#endif /* FALLOS_H */
