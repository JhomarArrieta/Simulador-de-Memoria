/*
 * pagetable.c - creacion y destruccion de la tabla de dos niveles, y las
 * operaciones alloc / free sobre el espacio virtual del proceso.
 */
#include "pagetable.h"

#include "mmu.h" /* mmu_vpn y compania: el formato de la VA vive en un solo lado */

#include <assert.h>
#include <stdlib.h>

/*
 * Verificacion en tiempo de compilacion: si la PTE no cabe en 4 bytes, el
 * arreglo queda de tamano -1 y gcc no compila. C99 no tiene _Static_assert.
 */
typedef char verificacion_tam_pte[(sizeof(pte_t) == 4) ? 1 : -1];

static tabla_nivel2_t *obtener_tabla(directorio_t *dir, uint32_t pt1);
static int             reservar_espacio_registro(directorio_t *dir);
static uint32_t        liberar_rango(directorio_t *dir, memoria_fisica_t *mem,
                                     area_swap_t *swap, uint32_t vpn_inicio,
                                     uint32_t paginas);

directorio_t *pagetable_crear(void)
{
    /* calloc y no malloc: deja las 1024 entradas en NULL y el bump pointer en 0,
       que es exactamente el estado inicial "el proceso no ha pedido nada". */
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
    free(dir->asignaciones);
    free(dir);
}

/* Tabla de nivel 2 de la region pt1, creandola si no existe. NULL si falla. */
static tabla_nivel2_t *obtener_tabla(directorio_t *dir, uint32_t pt1)
{
    assert(pt1 < ENTRADAS_NIVEL1);

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
    return dir->nivel1[pt1];
}

pte_t *pagetable_buscar_pte(directorio_t *dir, uint32_t pt1, uint32_t pt2)
{
    assert(dir != NULL);
    assert(pt1 < ENTRADAS_NIVEL1 && pt2 < ENTRADAS_NIVEL2);

    if (dir->nivel1[pt1] == NULL) {
        return NULL; /* esa region de 4 MB no tiene ninguna pagina valida */
    }
    return &dir->nivel1[pt1]->entradas[pt2];
}

/*
 * Asegura que cabe una asignacion mas en el registro, duplicando la capacidad
 * cuando se llena (costo amortizado O(1)). Devuelve 0 si falla realloc.
 */
static int reservar_espacio_registro(directorio_t *dir)
{
    asignacion_t *nuevo;
    size_t        cap;

    if (dir->num_asignaciones < dir->cap_asignaciones) {
        return 1;
    }

    cap   = (dir->cap_asignaciones == 0) ? 8 : dir->cap_asignaciones * 2;
    nuevo = realloc(dir->asignaciones, cap * sizeof(asignacion_t));
    if (nuevo == NULL) {
        return 0;
    }
    dir->asignaciones     = nuevo;
    dir->cap_asignaciones = cap;
    return 1;
}

resultado_alloc_t pagetable_alloc(directorio_t *dir, uint32_t bytes,
                                  uint32_t *va_inicio, uint32_t *paginas)
{
    uint32_t necesarias;
    uint32_t vpn_inicio;
    uint32_t i;

    assert(dir != NULL && va_inicio != NULL && paginas != NULL);

    *va_inicio = 0;
    *paginas   = 0;

    if (bytes == 0) {
        return ALLOC_CERO;
    }

    /* Redondeo hacia arriba a paginas completas. Se escribe asi y no como
       (bytes + TAM_PAGINA - 1) / TAM_PAGINA porque esa suma se desborda con un
       alloc cercano a 2^32 bytes. */
    necesarias = bytes / TAM_PAGINA + ((bytes % TAM_PAGINA != 0) ? 1 : 0);

    /* La resta va en este orden para no desbordar el uint32_t. */
    if (necesarias > PAGINAS_VIRTUALES - dir->proxima_pagina) {
        return ALLOC_SIN_ESPACIO;
    }

    /* El registro se agranda antes de tocar las PTEs: si falla, no hay nada que
       deshacer. */
    if (!reservar_espacio_registro(dir)) {
        return ALLOC_SIN_MEMORIA;
    }

    vpn_inicio = dir->proxima_pagina;

    for (i = 0; i < necesarias; i++) {
        uint32_t        vpn   = vpn_inicio + i;
        uint32_t        pt1   = mmu_pt1_de_vpn(vpn);
        tabla_nivel2_t *tabla = obtener_tabla(dir, pt1);
        pte_t          *pte;

        if (tabla == NULL) {
            /* Sin memoria a mitad del alloc: se deshace lo marcado para que el
               espacio virtual quede exactamente como estaba. Todavia no hay
               marcos asignados, asi que no hace falta la memoria fisica. */
            liberar_rango(dir, NULL, NULL, vpn_inicio, i);
            return ALLOC_SIN_MEMORIA;
        }

        pte = &tabla->entradas[mmu_pt2_de_vpn(vpn)];
        assert(!pte->valid); /* el bump pointer nunca vuelve atras */

        /* alloc solo promete espacio virtual: valid = 1 y present = 0. El marco
           llega en el primer acceso, que por eso es siempre un fallo. */
        pte->valid    = 1;
        pte->present  = 0;
        pte->accessed = 0;
        pte->dirty    = 0;
        pte->swapped  = 0;
        pte->pfn      = 0;
        tabla->paginas_validas++;
    }

    dir->asignaciones[dir->num_asignaciones].vpn_inicio = vpn_inicio;
    dir->asignaciones[dir->num_asignaciones].paginas    = necesarias;
    dir->num_asignaciones++;
    dir->proxima_pagina = vpn_inicio + necesarias;

    *va_inicio = mmu_va_de_vpn(vpn_inicio);
    *paginas   = necesarias;
    return ALLOC_OK;
}

/*
 * Invalida un rango de paginas y devuelve cuantos marcos se liberaron. 'mem' y
 * 'swap' pueden ser NULL cuando se sabe que ninguna pagina esta presente ni en
 * swap (el rollback de alloc). Tambien colapsa las tablas de nivel 2 que quedan
 * sin paginas validas.
 */
static uint32_t liberar_rango(directorio_t *dir, memoria_fisica_t *mem,
                              area_swap_t *swap, uint32_t vpn_inicio, uint32_t paginas)
{
    uint32_t i;
    uint32_t marcos_liberados = 0;

    for (i = 0; i < paginas; i++) {
        uint32_t        vpn   = vpn_inicio + i;
        uint32_t        pt1   = mmu_pt1_de_vpn(vpn);
        tabla_nivel2_t *tabla = dir->nivel1[pt1];
        pte_t          *pte;

        if (tabla == NULL) {
            continue; /* la tabla ya se colapso: no queda nada valido aqui */
        }

        pte = &tabla->entradas[mmu_pt2_de_vpn(vpn)];
        if (!pte->valid) {
            continue;
        }

        /* Si la pagina tenia marco, el marco vuelve a la lista de libres, y si
           tenia copia en swap, el slot vuelve al area de swap. Mientras la pagina
           esta presente el slot vive en el marco; fuera de memoria, en el pfn. */
        if (pte->present && mem != NULL) {
            long slot = physmem_slot_swap(mem, (int) pte->pfn);

            if (slot >= 0 && swap != NULL) {
                swap_liberar(swap, (uint32_t) slot);
            }
            physmem_devolver_marco(mem, (int) pte->pfn);
            marcos_liberados++;
        } else if (pte->swapped && swap != NULL) {
            swap_liberar(swap, pte->pfn);
        }

        pte->valid    = 0;
        pte->present  = 0;
        pte->accessed = 0;
        pte->dirty    = 0;
        pte->swapped  = 0;
        pte->pfn      = 0;
        tabla->paginas_validas--;

        /* Sin paginas validas la tabla no aporta nada: la memoria de traduccion
           baja igual que subio. Un acceso posterior a esta region encontrara
           NULL en el nivel 1, o sea acceso ilegal, que es lo correcto. */
        if (tabla->paginas_validas == 0) {
            free(tabla);
            dir->nivel1[pt1] = NULL;
            dir->tablas_creadas--;
        }
    }

    return marcos_liberados;
}

resultado_free_t pagetable_free(directorio_t *dir, memoria_fisica_t *mem, area_swap_t *swap,
                                uint32_t va, uint32_t *paginas_liberadas,
                                uint32_t *marcos_liberados)
{
    size_t i;

    assert(dir != NULL && mem != NULL && swap != NULL);
    assert(paginas_liberadas != NULL && marcos_liberados != NULL);

    *paginas_liberadas = 0;
    *marcos_liberados  = 0;

    for (i = 0; i < dir->num_asignaciones; i++) {
        /* Se exige la VA exacta que devolvio alloc, igual que free(ptr) en C: un
           puntero al medio del bloque no es un argumento valido. */
        if (mmu_va_de_vpn(dir->asignaciones[i].vpn_inicio) != va) {
            continue;
        }

        *paginas_liberadas = dir->asignaciones[i].paginas;
        *marcos_liberados  = liberar_rango(dir, mem, swap, dir->asignaciones[i].vpn_inicio,
                                           dir->asignaciones[i].paginas);

        /* Se saca del registro moviendo la ultima entrada al hueco: el orden del
           registro no significa nada, asi que no hay que desplazar el resto. */
        dir->asignaciones[i] = dir->asignaciones[dir->num_asignaciones - 1];
        dir->num_asignaciones--;
        return FREE_OK;
    }

    return FREE_NO_ES_INICIO;
}

size_t pagetable_tablas_nivel2(const directorio_t *dir)
{
    assert(dir != NULL);
    return dir->tablas_creadas;
}

size_t pagetable_memoria_usada(const directorio_t *dir)
{
    assert(dir != NULL);
    return sizeof(directorio_t)
         + dir->tablas_creadas * sizeof(tabla_nivel2_t)
         + dir->cap_asignaciones * sizeof(asignacion_t);
}
