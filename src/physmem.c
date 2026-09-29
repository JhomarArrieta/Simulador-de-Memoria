/*
 * physmem.c - marcos fisicos, su contenido y la lista de marcos libres.
 */
#include "physmem.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

memoria_fisica_t *physmem_crear(size_t kb)
{
    memoria_fisica_t *mf;
    size_t            bytes;
    int               num_marcos;
    int               i;

    if (kb < MEMORIA_FISICA_KB_MINIMA || kb > MEMORIA_FISICA_KB_MAXIMA) {
        return NULL;
    }

    bytes      = kb * 1024u;
    num_marcos = (int) (bytes / TAM_PAGINA); /* redondeo hacia abajo: marcos completos */
    if (num_marcos <= 0) {
        return NULL;
    }

    mf = calloc(1, sizeof(memoria_fisica_t));
    if (mf == NULL) {
        return NULL;
    }

    mf->marcos = calloc((size_t) num_marcos, sizeof(marco_t));
    mf->datos  = calloc((size_t) num_marcos, TAM_PAGINA);
    if (mf->marcos == NULL || mf->datos == NULL) {
        physmem_destruir(mf); /* tolera el objeto a medio construir */
        return NULL;
    }

    mf->num_marcos = num_marcos;
    mf->num_libres = num_marcos;

    /*
     * Lista de libres intrusiva: 0 -> 1 -> 2 -> ... -> n-1 -> -1. No necesita un
     * malloc por nodo, es O(1) al tomar y al devolver, y al estar en orden
     * ascendente los primeros fallos ocupan los marcos 0, 1, 2... lo que hace la
     * salida de las pruebas mucho mas facil de leer.
     */
    for (i = 0; i < num_marcos; i++) {
        mf->marcos[i].siguiente = i + 1;
        mf->marcos[i].slot_swap = -1;
    }
    mf->marcos[num_marcos - 1].siguiente = -1;
    mf->head_libre = 0;

    return mf;
}

void physmem_destruir(memoria_fisica_t *mf)
{
    if (mf == NULL) {
        return;
    }
    free(mf->marcos);
    free(mf->datos);
    free(mf);
}

int physmem_num_marcos(const memoria_fisica_t *mf)
{
    assert(mf != NULL);
    return mf->num_marcos;
}

int physmem_num_libres(const memoria_fisica_t *mf)
{
    assert(mf != NULL);
    return mf->num_libres;
}

int physmem_tomar_marco(memoria_fisica_t *mf, uint32_t vpn)
{
    int marco;

    assert(mf != NULL);

    if (mf->head_libre < 0) {
        assert(mf->num_libres == 0);
        return -1; /* memoria llena: fallos.c pide una victima a la politica */
    }

    marco          = mf->head_libre;
    mf->head_libre = mf->marcos[marco].siguiente;
    mf->num_libres--;

    mf->marcos[marco].ocupado   = 1;
    mf->marcos[marco].vpn       = vpn;
    mf->marcos[marco].siguiente = -1;
    mf->marcos[marco].slot_swap = -1;

    /* Demand zeroing: la pagina que se entrega no debe mostrar los datos del
       proceso (o de la pagina) que ocupaba antes ese marco. */
    memset(mf->datos + (size_t) marco * TAM_PAGINA, 0, TAM_PAGINA);

    return marco;
}

void physmem_devolver_marco(memoria_fisica_t *mf, int marco)
{
    assert(mf != NULL);
    assert(marco >= 0 && marco < mf->num_marcos);
    assert(mf->marcos[marco].ocupado); /* devolver un marco libre es un bug */

    mf->marcos[marco].ocupado   = 0;
    mf->marcos[marco].vpn       = 0;
    mf->marcos[marco].slot_swap = -1;
    mf->marcos[marco].siguiente = mf->head_libre;
    mf->head_libre              = marco;
    mf->num_libres++;
}

uint32_t physmem_vpn_de(const memoria_fisica_t *mf, int marco)
{
    assert(mf != NULL);
    assert(marco >= 0 && marco < mf->num_marcos);
    assert(mf->marcos[marco].ocupado);
    return mf->marcos[marco].vpn;
}

long physmem_slot_swap(const memoria_fisica_t *mf, int marco)
{
    assert(mf != NULL);
    assert(marco >= 0 && marco < mf->num_marcos && mf->marcos[marco].ocupado);
    return mf->marcos[marco].slot_swap;
}

void physmem_fijar_slot_swap(memoria_fisica_t *mf, int marco, long slot)
{
    assert(mf != NULL);
    assert(marco >= 0 && marco < mf->num_marcos && mf->marcos[marco].ocupado);
    mf->marcos[marco].slot_swap = slot;
}

unsigned char *physmem_datos_marco(memoria_fisica_t *mf, int marco)
{
    assert(mf != NULL);
    assert(marco >= 0 && marco < mf->num_marcos);
    return mf->datos + (size_t) marco * TAM_PAGINA;
}

const unsigned char *physmem_datos_marco_const(const memoria_fisica_t *mf, int marco)
{
    assert(mf != NULL);
    assert(marco >= 0 && marco < mf->num_marcos);
    return mf->datos + (size_t) marco * TAM_PAGINA;
}

void physmem_escribir_palabra(memoria_fisica_t *mf, uint32_t pa, uint32_t valor)
{
    assert(mf != NULL);
    /* Una PA fuera de rango o desalineada significa que la traduccion produjo
       basura: es un bug del simulador, no un error del programa de entrada. */
    assert(pa % TAM_PALABRA == 0);
    assert((size_t) pa + TAM_PALABRA <= (size_t) mf->num_marcos * TAM_PAGINA);
    memcpy(mf->datos + pa, &valor, TAM_PALABRA); /* memcpy evita accesos desalineados */
}

uint32_t physmem_leer_palabra(const memoria_fisica_t *mf, uint32_t pa)
{
    uint32_t valor;

    assert(mf != NULL);
    assert(pa % TAM_PALABRA == 0);
    assert((size_t) pa + TAM_PALABRA <= (size_t) mf->num_marcos * TAM_PAGINA);
    memcpy(&valor, mf->datos + pa, TAM_PALABRA);
    return valor;
}

size_t physmem_memoria_usada(const memoria_fisica_t *mf)
{
    assert(mf != NULL);
    return sizeof(memoria_fisica_t)
         + (size_t) mf->num_marcos * sizeof(marco_t)
         + (size_t) mf->num_marcos * TAM_PAGINA;
}
