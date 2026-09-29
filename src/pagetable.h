/*
 * pagetable.h - tabla de paginas de dos niveles.
 *
 * El directorio (nivel 1) existe desde el arranque; las tablas de nivel 2 se
 * crean bajo demanda la primera vez que se toca su region de 4 MB. Esa es la
 * ventaja de la tabla multinivel: el espacio virtual que no se usa no cuesta
 * memoria (OSTEP cap. 20).
 *
 * Aqui no se sabe nada de direcciones virtuales: las funciones reciben los
 * indices pt1 y pt2 ya extraidos. Quien los extrae es la MMU (fase 3).
 */
#ifndef PAGETABLE_H
#define PAGETABLE_H

#include "config.h"

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

    /* Cuantas entradas tienen valid = 1. Permite detectar que la tabla quedo
       vacia y se puede liberar; si se colapsa o no lo decide la fase 4. */
    size_t paginas_validas;
} tabla_nivel2_t;

typedef struct {
    /* NULL significa "esta region de 4 MB nunca se toco". El indice pt1 ya es la
       posicion, asi que la busqueda es un acceso directo, sin recorrer nada. */
    tabla_nivel2_t *nivel1[ENTRADAS_NIVEL1];

    size_t tablas_creadas; /* tablas de nivel 2 vivas, para el reporte */
} directorio_t;

/* Crea el directorio con las 1024 entradas en NULL. NULL si falla la memoria. */
directorio_t *pagetable_crear(void);

/* Libera el directorio y todas las tablas de nivel 2 que existan. Tolera NULL. */
void pagetable_destruir(directorio_t *dir);

/*
 * Devuelve la PTE de (pt1, pt2), o NULL si la tabla de nivel 2 no existe todavia.
 * No crea nada: es la consulta que hace la traduccion.
 */
pte_t *pagetable_buscar_pte(directorio_t *dir, uint32_t pt1, uint32_t pt2);

/*
 * Igual que la anterior, pero creando la tabla de nivel 2 si hace falta.
 * NULL solo si falla la asignacion de memoria. La usa alloc (fase 4).
 */
pte_t *pagetable_obtener_pte(directorio_t *dir, uint32_t pt1, uint32_t pt2);

/* Tablas de nivel 2 vivas y bytes que ocupan las estructuras de traduccion. */
size_t pagetable_tablas_nivel2(const directorio_t *dir);
size_t pagetable_memoria_usada(const directorio_t *dir);

#endif /* PAGETABLE_H */
