/*
 * swap.h - area de intercambio (swap) simulada.
 *
 * Cuando la politica desaloja una pagina sucia, su contenido se copia aqui antes
 * de soltar el marco; cuando la pagina vuelve a usarse, el fallo la trae de aqui
 * en vez de entregar un marco en ceros (OSTEP cap. 21.1 y 21.3).
 *
 * El swap se organiza en "slots" del tamano de una pagina. Mientras una pagina
 * esta fuera de memoria (present = 0, swapped = 1), el numero de su slot se
 * guarda en el campo pfn de la PTE: OSTEP cap. 21.3 propone exactamente eso,
 * reusar los bits del PFN para la direccion en disco. Mientras la pagina esta en
 * memoria, el slot se guarda en los metadatos del marco (physmem.h).
 */
#ifndef SWAP_H
#define SWAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned char **paginas;    /* paginas[slot]: copia de TAM_PAGINA bytes, NULL si libre */
    size_t          num_slots;  /* slots creados alguna vez (ocupados + libres) */
    size_t          cap_slots;
    uint32_t       *libres;     /* pila de slots liberados, para reutilizarlos */
    size_t          num_libres;
    size_t          cap_libres;
    size_t          en_uso;     /* slots con una pagina guardada */
    size_t          pico;       /* maximo de slots en uso a la vez */
} area_swap_t;

/* Crea un area vacia. NULL si falla la memoria. */
area_swap_t *swap_crear(void);

/* Libera todas las copias y el area. Tolera NULL. */
void swap_destruir(area_swap_t *swap);

/* Reserva un slot para una pagina. Devuelve 1 y deja el numero en *slot, o 0 si
   no hubo memoria. */
int swap_reservar(area_swap_t *swap, uint32_t *slot);

/* Copia una pagina completa de la memoria fisica al slot. */
void swap_escribir(area_swap_t *swap, uint32_t slot, const unsigned char *pagina);

/* Copia el contenido del slot a un marco de la memoria fisica. */
void swap_leer(const area_swap_t *swap, uint32_t slot, unsigned char *destino);

/* Devuelve el slot al area (la pagina se libero con free). */
void swap_liberar(area_swap_t *swap, uint32_t slot);

size_t swap_slots_en_uso(const area_swap_t *swap);
size_t swap_pico_slots(const area_swap_t *swap);

#endif /* SWAP_H */
