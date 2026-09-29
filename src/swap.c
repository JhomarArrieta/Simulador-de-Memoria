/*
 * swap.c - slots de intercambio en memoria del simulador.
 */
#include "swap.h"

#include "config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

area_swap_t *swap_crear(void)
{
    /* calloc: arreglos en NULL y contadores en 0. */
    return calloc(1, sizeof(area_swap_t));
}

void swap_destruir(area_swap_t *swap)
{
    size_t i;

    if (swap == NULL) {
        return;
    }
    for (i = 0; i < swap->num_slots; i++) {
        free(swap->paginas[i]); /* free(NULL) es valido para los slots libres */
    }
    free(swap->paginas);
    free(swap->libres);
    free(swap);
}

/* Asegura que la pila de libres pueda recibir todos los slots creados, mas el
   que se va a crear ahora. Duplica la capacidad: costo amortizado O(1). Se hace
   por adelantado para que swap_liberar nunca necesite memoria y no pueda fallar. */
static int crecer_libres(area_swap_t *swap)
{
    uint32_t *nuevo;
    size_t    cap;

    if (swap->num_slots + 1 <= swap->cap_libres) {
        return 1;
    }
    cap   = (swap->cap_libres == 0) ? 64 : swap->cap_libres * 2;
    nuevo = realloc(swap->libres, cap * sizeof(uint32_t));
    if (nuevo == NULL) {
        return 0;
    }
    swap->libres     = nuevo;
    swap->cap_libres = cap;
    return 1;
}

/* Asegura espacio para un slot nuevo en el arreglo de paginas. */
static int crecer_slots(area_swap_t *swap)
{
    unsigned char **nuevo;
    size_t          cap;

    if (swap->num_slots < swap->cap_slots) {
        return 1;
    }
    cap   = (swap->cap_slots == 0) ? 64 : swap->cap_slots * 2;
    nuevo = realloc(swap->paginas, cap * sizeof(unsigned char *));
    if (nuevo == NULL) {
        return 0;
    }
    swap->paginas   = nuevo;
    swap->cap_slots = cap;
    return 1;
}

int swap_reservar(area_swap_t *swap, uint32_t *slot)
{
    unsigned char *pagina;
    uint32_t       elegido;

    assert(swap != NULL && slot != NULL);

    if (!crecer_libres(swap)) {
        return 0;
    }

    pagina = malloc(TAM_PAGINA);
    if (pagina == NULL) {
        return 0;
    }

    if (swap->num_libres > 0) {
        elegido = swap->libres[--swap->num_libres];
    } else {
        if (!crecer_slots(swap)) {
            free(pagina);
            return 0;
        }
        elegido                = (uint32_t) swap->num_slots++;
        swap->paginas[elegido] = NULL;
    }

    assert(swap->paginas[elegido] == NULL);
    swap->paginas[elegido] = pagina;
    swap->en_uso++;
    if (swap->en_uso > swap->pico) {
        swap->pico = swap->en_uso;
    }
    *slot = elegido;
    return 1;
}

void swap_escribir(area_swap_t *swap, uint32_t slot, const unsigned char *pagina)
{
    assert(swap != NULL && slot < swap->num_slots && swap->paginas[slot] != NULL);
    memcpy(swap->paginas[slot], pagina, TAM_PAGINA);
}

void swap_leer(const area_swap_t *swap, uint32_t slot, unsigned char *destino)
{
    assert(swap != NULL && slot < swap->num_slots && swap->paginas[slot] != NULL);
    memcpy(destino, swap->paginas[slot], TAM_PAGINA);
}

void swap_liberar(area_swap_t *swap, uint32_t slot)
{
    assert(swap != NULL && slot < swap->num_slots && swap->paginas[slot] != NULL);
    free(swap->paginas[slot]);
    swap->paginas[slot]            = NULL;
    swap->libres[swap->num_libres++] = slot; /* capacidad garantizada al reservar */
    swap->en_uso--;
}

size_t swap_slots_en_uso(const area_swap_t *swap)
{
    assert(swap != NULL);
    return swap->en_uso;
}

size_t swap_pico_slots(const area_swap_t *swap)
{
    assert(swap != NULL);
    return swap->pico;
}
