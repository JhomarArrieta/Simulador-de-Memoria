/*
 * mmu.h - traduccion de direccion virtual a direccion fisica.
 *
 * La MMU es la unica parte del simulador que conoce el formato de la direccion
 * virtual. El resto del codigo trabaja con pt1, pt2, offset o vpn ya extraidos.
 *
 *  31            22 21            12 11                 0
 * +----------------+----------------+-------------------+
 * |   PT1 (10 b)   |   PT2 (10 b)   |   offset (12 b)   |
 * +----------------+----------------+-------------------+
 */
#ifndef MMU_H
#define MMU_H

#include "config.h"
#include "pagetable.h"

#include <stdint.h>

typedef enum {
    TRAD_OK,           /* pagina presente en memoria: la PA es valida        */
    TRAD_FALLO_PAGINA, /* valid = 1, present = 0: hay que traerla a un marco */
    TRAD_SEGFAULT      /* valid = 0 o la tabla de nivel 2 no existe          */
} resultado_trad_t;

typedef struct {
    resultado_trad_t resultado;
    uint32_t         pa;  /* solo tiene sentido si resultado == TRAD_OK */
    /*
     * PTE del acceso, o NULL si la tabla de nivel 2 no existe. Se devuelve para
     * que quien ejecuta el acceso marque accessed/dirty o cargue el marco sin
     * tener que recorrer otra vez los dos niveles.
     */
    pte_t           *pte;
} traduccion_t;

/* Extraccion de campos: mascaras y corrimientos, sin efectos secundarios. */
uint32_t mmu_pt1(uint32_t va);
uint32_t mmu_pt2(uint32_t va);
uint32_t mmu_offset(uint32_t va);

/* Numero de pagina virtual: los bits por encima del offset, pt1 << BITS_NIVEL2 | pt2. */
uint32_t mmu_vpn(uint32_t va);

/* Partes de un vpn, para ubicar la PTE de la pagina que se desaloja. */
uint32_t mmu_pt1_de_vpn(uint32_t vpn);
uint32_t mmu_pt2_de_vpn(uint32_t vpn);

/* Primera direccion virtual de una pagina: el inverso de mmu_vpn. */
uint32_t mmu_va_de_vpn(uint32_t vpn);

/* Arma la direccion fisica. El offset nunca cambia: pa = pfn * TAM_PAGINA + offset. */
uint32_t mmu_armar_pa(uint32_t pfn, uint32_t offset);

/*
 * Recorre el directorio y la tabla de nivel 2. No modifica ningun bit de la PTE:
 * decidir que hacer con un fallo o con un acceso ilegal es de otra capa.
 */
traduccion_t mmu_traducir(directorio_t *dir, uint32_t va);

#endif /* MMU_H */
