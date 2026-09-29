/*
 * pagetable.c - creacion, consulta y destruccion de la tabla de dos niveles.
 */
#include "pagetable.h"

#include <assert.h>
#include <stdlib.h>

/*
 * Verificacion en tiempo de compilacion: si la PTE no cabe en 4 bytes, el
 * arreglo queda de tamano -1 y gcc no compila. C99 no tiene _Static_assert.
 */
typedef char verificacion_tam_pte[(sizeof(pte_t) == 4) ? 1 : -1];

directorio_t *pagetable_crear(void)
{
    /* calloc y no malloc: deja las 1024 entradas en NULL, que es exactamente el
       estado inicial "ninguna region del espacio virtual se ha tocado". */
    return calloc(1, sizeof(directorio_t));
}

void pagetable_destruir(directorio_t *dir)
{
    uint32_t i;

    if (dir == NULL) {
        return;
    }

    for (i = 0; i < ENTRADAS_NIVEL1; i++) {
        free(dir->nivel1[i]); /* free(NULL) es valido, no hace falta preguntar */
    }
    free(dir);
}

pte_t *pagetable_buscar_pte(directorio_t *dir, uint32_t pt1, uint32_t pt2)
{
    assert(dir != NULL);
    assert(pt1 < ENTRADAS_NIVEL1 && pt2 < ENTRADAS_NIVEL2);

    if (dir->nivel1[pt1] == NULL) {
        return NULL; /* la region de 4 MB nunca se toco */
    }
    return &dir->nivel1[pt1]->entradas[pt2];
}

pte_t *pagetable_obtener_pte(directorio_t *dir, uint32_t pt1, uint32_t pt2)
{
    assert(dir != NULL);
    assert(pt1 < ENTRADAS_NIVEL1 && pt2 < ENTRADAS_NIVEL2);

    if (dir->nivel1[pt1] == NULL) {
        /* calloc otra vez por el estado inicial: valid = 0 y present = 0 en las
           1024 entradas, que es lo correcto para una region recien creada. */
        tabla_nivel2_t *tabla = calloc(1, sizeof(tabla_nivel2_t));

        if (tabla == NULL) {
            return NULL;
        }
        dir->nivel1[pt1] = tabla;
        dir->tablas_creadas++;
    }
    return &dir->nivel1[pt1]->entradas[pt2];
}

size_t pagetable_tablas_nivel2(const directorio_t *dir)
{
    assert(dir != NULL);
    return dir->tablas_creadas;
}

size_t pagetable_memoria_usada(const directorio_t *dir)
{
    assert(dir != NULL);
    return sizeof(directorio_t) + dir->tablas_creadas * sizeof(tabla_nivel2_t);
}
