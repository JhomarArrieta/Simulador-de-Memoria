/*
 * mmu.c - extraccion de campos de la direccion virtual y recorrido de la tabla.
 */
#include "mmu.h"

#include <assert.h>

/* Mascaras derivadas del formato, no numeros magicos sueltos. */
#define MASCARA_NIVEL1 ((1u << BITS_NIVEL1) - 1u) /* 0x3FF */
#define MASCARA_NIVEL2 ((1u << BITS_NIVEL2) - 1u) /* 0x3FF */
#define MASCARA_OFFSET ((1u << BITS_OFFSET) - 1u) /* 0xFFF */

uint32_t mmu_pt1(uint32_t va)
{
    return (va >> (BITS_NIVEL2 + BITS_OFFSET)) & MASCARA_NIVEL1; /* va >> 22 */
}

uint32_t mmu_pt2(uint32_t va)
{
    return (va >> BITS_OFFSET) & MASCARA_NIVEL2; /* va >> 12 */
}

uint32_t mmu_offset(uint32_t va)
{
    return va & MASCARA_OFFSET;
}

uint32_t mmu_vpn(uint32_t va)
{
    /* Equivale a mmu_pt1(va) << BITS_NIVEL2 | mmu_pt2(va): los 20 bits altos. */
    return va >> BITS_OFFSET;
}

uint32_t mmu_pt1_de_vpn(uint32_t vpn)
{
    return (vpn >> BITS_NIVEL2) & MASCARA_NIVEL1;
}

uint32_t mmu_pt2_de_vpn(uint32_t vpn)
{
    return vpn & MASCARA_NIVEL2;
}

uint32_t mmu_va_de_vpn(uint32_t vpn)
{
    return vpn << BITS_OFFSET;
}

uint32_t mmu_armar_pa(uint32_t pfn, uint32_t offset)
{
    assert(offset <= MASCARA_OFFSET); /* un offset mayor pisaria el pfn */
    return (pfn << BITS_OFFSET) | offset;
}

traduccion_t mmu_traducir(directorio_t *dir, uint32_t va)
{
    traduccion_t t;
    pte_t       *pte;

    assert(dir != NULL);

    t.resultado = TRAD_SEGFAULT;
    t.pa        = 0;
    t.pte       = NULL;

    /* Primer nivel. Si la region de 4 MB nunca se toco, no hay tabla que bajar y
       la direccion no pertenece al proceso. */
    pte = pagetable_buscar_pte(dir, mmu_pt1(va), mmu_pt2(va));
    if (pte == NULL) {
        return t;
    }
    t.pte = pte;

    /* valid = 0: la pagina nunca se asigno. Acceso ilegal, no es un fallo. */
    if (!pte->valid) {
        return t;
    }

    /* valid = 1, present = 0: la pagina existe pero no esta en un marco. */
    if (!pte->present) {
        t.resultado = TRAD_FALLO_PAGINA;
        return t;
    }

    t.resultado = TRAD_OK;
    t.pa        = mmu_armar_pa(pte->pfn, mmu_offset(va));
    return t;
}
