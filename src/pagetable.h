/*
 * pagetable.h - tabla de paginas de dos niveles y el espacio virtual del proceso.
 *
 * El directorio (nivel 1) existe desde el arranque; las tablas de nivel 2 se
 * crean bajo demanda la primera vez que se toca su region de 4 MB, y se liberan
 * cuando se queda sin paginas validas. Esa es la ventaja de la tabla multinivel:
 * el espacio virtual que no se usa no cuesta memoria (OSTEP cap. 20).
 */
#ifndef PAGETABLE_H
#define PAGETABLE_H

#include "config.h"
#include "physmem.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Entrada de la tabla de nivel 2. Campos de bits para que quepa en 4 bytes,
 * igual que una PTE real de x86 de 32 bits, donde los bits de control viven en
 * la misma palabra que el numero de marco (OSTEP cap. 18.3).
 *
 * La distincion que importa:
 *   valid = 0              -> la pagina nunca se asigno: acceso ilegal.
 *   valid = 1, present = 0 -> la pagina existe pero no esta en memoria: fallo.
 */
typedef struct {
    unsigned int pfn      : 20; /* marco fisico; 20 bits cubren las 2^20 paginas */
    unsigned int valid    : 1;  /* la pagina fue asignada por el proceso */
    unsigned int present  : 1;  /* la pagina esta en memoria fisica */
    unsigned int accessed : 1;  /* referenciada por un read o un write */
    unsigned int dirty    : 1;  /* modificada por un write */
} pte_t;

typedef struct {
    pte_t  entradas[ENTRADAS_NIVEL2];

    /* Cuantas entradas tienen valid = 1. Lo mantienen pagetable_alloc y
       pagetable_free, y sirve para saber cuando la tabla quedo vacia y se
       puede liberar. */
    size_t paginas_validas;
} tabla_nivel2_t;

/*
 * Una asignacion viva, tal como la devolvio un alloc. El registro existe porque
 * free recibe una direccion y tiene que liberar la asignacion completa, igual
 * que free(ptr) en C libera todo el bloque que devolvio malloc.
 */
typedef struct {
    uint32_t vpn_inicio;
    uint32_t paginas;
} asignacion_t;

typedef struct {
    /* NULL significa "esta region de 4 MB no tiene ninguna pagina valida". El
       indice pt1 ya es la posicion, asi que la busqueda es un acceso directo. */
    tabla_nivel2_t *nivel1[ENTRADAS_NIVEL1];
    size_t          tablas_creadas; /* tablas de nivel 2 vivas */

    /*
     * Bump pointer del espacio virtual, en numero de pagina. Arranca en 0 y solo
     * sube, igual que el heap con brk/sbrk: el espacio liberado no se reutiliza.
     */
    uint32_t proxima_pagina;

    asignacion_t *asignaciones;     /* arreglo dinamico de asignaciones vivas */
    size_t        num_asignaciones;
    size_t        cap_asignaciones;
} directorio_t;

typedef enum {
    ALLOC_OK,
    ALLOC_CERO,        /* alloc de 0 bytes: no hay nada que asignar */
    ALLOC_SIN_ESPACIO, /* no cabe en el espacio virtual de 32 bits  */
    ALLOC_SIN_MEMORIA  /* fallo al pedir memoria para las tablas    */
} resultado_alloc_t;

typedef enum {
    FREE_OK,
    FREE_NO_ES_INICIO /* la VA no es el inicio de ninguna asignacion viva */
} resultado_free_t;

/* Crea el directorio con las 1024 entradas en NULL. NULL si falla la memoria. */
directorio_t *pagetable_crear(void);

/* Libera el directorio, las tablas de nivel 2 y el registro. Tolera NULL. */
void pagetable_destruir(directorio_t *dir);

/*
 * Devuelve la PTE de (pt1, pt2), o NULL si la tabla de nivel 2 no existe.
 * No crea nada: es la consulta que hace la traduccion.
 */
pte_t *pagetable_buscar_pte(directorio_t *dir, uint32_t pt1, uint32_t pt2);

/*
 * Asigna 'bytes' redondeados a paginas completas, empezando en el bump pointer.
 * Marca las paginas como valid = 1 y present = 0: NO reserva memoria fisica, los
 * marcos se asignan en el primer acceso (demand paging). Devuelve en *va_inicio y
 * *paginas lo asignado. Si falla, el espacio virtual queda como estaba.
 */
resultado_alloc_t pagetable_alloc(directorio_t *dir, uint32_t bytes,
                                  uint32_t *va_inicio, uint32_t *paginas);

/*
 * Libera la asignacion que empieza exactamente en 'va'. Pone valid = 0 en todas
 * sus paginas, devuelve a la lista de libres los marcos de las que estaban
 * presentes y libera las tablas de nivel 2 que queden sin paginas validas.
 */
resultado_free_t pagetable_free(directorio_t *dir, memoria_fisica_t *mem, uint32_t va,
                                uint32_t *paginas_liberadas, uint32_t *marcos_liberados);

/* Tablas de nivel 2 vivas y bytes que ocupan las estructuras de traduccion. */
size_t pagetable_tablas_nivel2(const directorio_t *dir);
size_t pagetable_memoria_usada(const directorio_t *dir);

#endif /* PAGETABLE_H */
