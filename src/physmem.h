/*
 * physmem.h - memoria fisica simulada: marcos, contenido y lista de libres.
 *
 * No reserva memoria real "para el proceso simulado": modela la memoria fisica
 * como un arreglo de marcos de 4 KB mas los metadatos de cada marco.
 */
#ifndef PHYSMEM_H
#define PHYSMEM_H

#include "config.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int      ocupado;
    /*
     * Pagina virtual que vive en este marco (pt1 << BITS_NIVEL2 | pt2). Es el mapeo
     * inverso: al desalojar una victima hay que poner present = 0 en SU PTE, y
     * para eso el marco tiene que saber de quien es. Se guarda el numero y no un
     * pte_t* para no quedar con un puntero colgante si free libera la tabla
     * de nivel 2 mientras el marco sigue ocupado.
     */
    uint32_t vpn;
    int      siguiente; /* siguiente marco de la lista de libres, o -1 */

    /*
     * Slot de swap que guarda una copia de esta pagina, o -1 si no tiene. Mientras
     * la pagina esta en memoria el campo pfn de su PTE contiene el marco, asi que
     * el slot se recuerda aqui; al desalojarla vuelve al pfn de la PTE (swap.h).
     */
    long     slot_swap;
} marco_t;

typedef struct {
    marco_t       *marcos;
    unsigned char *datos;      /* num_marcos * TAM_PAGINA bytes contiguos */
    int            num_marcos;
    int            head_libre; /* cabeza de la lista de libres, o -1 */
    int            num_libres;
} memoria_fisica_t;

/*
 * Crea la memoria fisica de 'kb' kilobytes. Devuelve NULL si kb es menor que el
 * minimo del enunciado (256 KB) o si falla la asignacion. Si kb no es multiplo
 * del tamano de pagina se redondea hacia abajo, a marcos completos.
 */
memoria_fisica_t *physmem_crear(size_t kb);

/* Libera todo. Tolera NULL y objetos a medio construir. */
void physmem_destruir(memoria_fisica_t *mf);

int physmem_num_marcos(const memoria_fisica_t *mf);
int physmem_num_libres(const memoria_fisica_t *mf);

/*
 * Saca un marco de la lista de libres y lo asocia a la pagina virtual 'vpn'.
 * Devuelve el numero de marco, o -1 si no quedan libres (ahi entra la politica
 * de reemplazo). El contenido del marco se pone en cero, igual que un SO real
 * hace demand zeroing al entregar una pagina nueva; si la pagina viene del
 * swap, el manejador de fallos sobrescribe esos ceros con la copia guardada.
 */
int physmem_tomar_marco(memoria_fisica_t *mf, uint32_t vpn);

/* Devuelve un marco a la lista de libres. */
void physmem_devolver_marco(memoria_fisica_t *mf, int marco);

uint32_t physmem_vpn_de(const memoria_fisica_t *mf, int marco);

/*
 * Slot de swap asociado a la pagina que ocupa el marco (-1 si no tiene). Lo usa
 * el manejador de fallos al cargar desde swap y al desalojar.
 */
long physmem_slot_swap(const memoria_fisica_t *mf, int marco);
void physmem_fijar_slot_swap(memoria_fisica_t *mf, int marco, long slot);

/* Contenido completo de un marco, para copiarlo a o desde el swap. */
unsigned char       *physmem_datos_marco(memoria_fisica_t *mf, int marco);
const unsigned char *physmem_datos_marco_const(const memoria_fisica_t *mf, int marco);

/*
 * Acceso al contenido por direccion fisica, que es lo que produce la MMU. La
 * unidad es una palabra de 32 bits (TAM_PALABRA bytes): un write de 300 guarda
 * 300. La direccion debe estar alineada a 4, asi una palabra nunca cruza el
 * limite entre dos paginas; fallos.c rechaza las direcciones no alineadas antes
 * de traducir.
 */
void     physmem_escribir_palabra(memoria_fisica_t *mf, uint32_t pa, uint32_t valor);
uint32_t physmem_leer_palabra(const memoria_fisica_t *mf, uint32_t pa);

/* Bytes que ocupa toda la memoria fisica simulada, metadatos incluidos. */
size_t physmem_memoria_usada(const memoria_fisica_t *mf);

#endif /* PHYSMEM_H */
