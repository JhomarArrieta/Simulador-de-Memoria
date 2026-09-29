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
     * Pagina virtual que vive en este marco (pt1 << 10 | pt2). Es el mapeo
     * inverso: al desalojar una victima hay que poner present = 0 en SU PTE, y
     * para eso el marco tiene que saber de quien es. Se guarda el numero y no un
     * pte_t* para no quedar con un puntero colgante si la fase 4 libera la tabla
     * de nivel 2 mientras el marco sigue ocupado.
     */
    uint32_t vpn;
    int      siguiente; /* siguiente marco de la lista de libres, o -1 */
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
 * de reemplazo, en la fase 6). El contenido del marco se pone en cero, igual
 * que un SO real hace demand zeroing al entregar una pagina nueva.
 */
int physmem_tomar_marco(memoria_fisica_t *mf, uint32_t vpn);

/* Devuelve un marco a la lista de libres. */
void physmem_devolver_marco(memoria_fisica_t *mf, int marco);

int      physmem_marco_ocupado(const memoria_fisica_t *mf, int marco);
uint32_t physmem_vpn_de(const memoria_fisica_t *mf, int marco);

/*
 * Acceso al contenido por direccion fisica, que es lo que produce la MMU.
 * Un byte por direccion: mantiene la correspondencia VA <-> byte exacta y evita
 * que un valor de varios bytes se parta entre dos paginas.
 */
void          physmem_escribir_byte(memoria_fisica_t *mf, uint32_t pa, unsigned char valor);
unsigned char physmem_leer_byte(const memoria_fisica_t *mf, uint32_t pa);

/* Bytes que ocupa toda la memoria fisica simulada, metadatos incluidos. */
size_t physmem_memoria_usada(const memoria_fisica_t *mf);

#endif /* PHYSMEM_H */
