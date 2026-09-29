/*
 * fallos.c - manejo de fallos de pagina y ejecucion del acceso.
 */
#include "fallos.h"

#include "mmu.h"
#include "replace.h"

#include <assert.h>

static acceso_t ejecutar(sistema_memoria_t *sis, stats_t *st, uint32_t va,
                         int es_escritura, uint32_t valor);
static int      atender_fallo(sistema_memoria_t *sis, stats_t *st, pte_t *pte, uint32_t va,
                              acceso_t *acc);
static int      desalojar_victima(sistema_memoria_t *sis, stats_t *st, uint32_t *vpn_victima);

acceso_t acceso_leer(sistema_memoria_t *sis, stats_t *st, uint32_t va)
{
    return ejecutar(sis, st, va, 0, 0);
}

acceso_t acceso_escribir(sistema_memoria_t *sis, stats_t *st, uint32_t va, uint32_t valor)
{
    return ejecutar(sis, st, va, 1, valor);
}

/*
 * Un acceso, de principio a fin. El orden importa: primero se resuelve si la
 * pagina se puede usar, y solo cuando esta presente se toca la memoria y se
 * cuentan las estadisticas.
 */
static acceso_t ejecutar(sistema_memoria_t *sis, stats_t *st, uint32_t va,
                         int es_escritura, uint32_t valor)
{
    acceso_t     acc;
    traduccion_t t;

    assert(sis != NULL && sis->dir != NULL && sis->mem != NULL && sis->swap != NULL);
    assert(st != NULL);

    acc.resultado   = ACCESO_ILEGAL;
    acc.pa          = 0;
    acc.marco       = -1;
    acc.valor       = 0;
    acc.desde_swap  = 0;
    acc.vpn_victima = -1;

    /*
     * La unidad de acceso es una palabra de 32 bits. Exigir alineacion a 4 hace
     * que una palabra nunca cruce el limite entre dos paginas: un acceso es una
     * sola traduccion. No cuenta como acceso, igual que el ilegal.
     */
    if (va % TAM_PALABRA != 0) {
        stats_registrar_no_alineado(st);
        acc.resultado = ACCESO_NO_ALINEADO;
        return acc;
    }

    t = mmu_traducir(sis->dir, va);

    /*
     * valid = 0 (o sin tabla de nivel 2): la pagina nunca se asigno. Es un acceso
     * ilegal, el equivalente al segfault, y NO es un fallo de pagina: no se trae
     * nada a memoria y no cuenta como acceso, porque nunca se completo.
     */
    if (t.resultado == TRAD_SEGFAULT) {
        stats_registrar_ilegal(st);
        return acc;
    }

    if (t.resultado == TRAD_FALLO_PAGINA) {
        /* valid = 1, present = 0: la pagina existe pero no esta en un marco. */
        assert(t.pte != NULL);
        if (!atender_fallo(sis, st, t.pte, va, &acc)) {
            acc.resultado = ACCESO_ERROR_INTERNO;
            return acc;
        }
        /* La PA que traia la traduccion no servia: la pagina no tenia marco. */
        t.pa          = mmu_armar_pa(t.pte->pfn, mmu_offset(va));
        acc.resultado = ACCESO_FALLO_RESUELTO;
    } else {
        assert(t.resultado == TRAD_OK);
        acc.resultado = ACCESO_HIT;
    }

    /* De aqui en adelante la pagina esta presente y el acceso se completa. */
    assert(t.pte != NULL && t.pte->valid && t.pte->present);

    t.pte->accessed = 1; /* referenciada: la usa el algoritmo del reloj de un SO real */
    if (es_escritura) {
        t.pte->dirty = 1; /* modificada: al desalojarla habra que escribirla al swap */
        physmem_escribir_palabra(sis->mem, t.pa, valor);
        acc.valor = valor;
    } else {
        acc.valor = physmem_leer_palabra(sis->mem, t.pa);
    }

    acc.pa    = t.pa;
    acc.marco = (int) t.pte->pfn;

    /*
     * En CADA acceso, hit o fallo. En FIFO no hace nada; en LRU es lo que
     * actualiza el ultimo uso. Olvidar esta llamada en los hits es el error
     * clasico de LRU, y por eso va aqui y no dentro del manejo del fallo.
     */
    politica_al_acceder(acc.marco);

    stats_registrar_acceso(st, es_escritura);
    return acc;
}

/*
 * Manejo del fallo de pagina (requisito funcional 4):
 *   1. Asignar un marco libre.
 *   2. Si no hay, pedir una victima a la politica de reemplazo y desalojarla.
 *   3. Si la pagina tiene copia en swap, traerla; si no, el marco queda en ceros.
 *   4. Actualizar la PTE y avisar a la politica que el marco se lleno.
 * Devuelve 0 solo ante un error interno del simulador.
 */
static int atender_fallo(sistema_memoria_t *sis, stats_t *st, pte_t *pte, uint32_t va,
                         acceso_t *acc)
{
    int      marco = physmem_tomar_marco(sis->mem, mmu_vpn(va));
    uint32_t vpn_victima;

    if (marco < 0) {
        /* Memoria llena: la politica elige una victima, se invalida su PTE y su
           marco vuelve a la lista de libres. El marco que se acaba de liberar es
           la cabeza de esa lista, asi que es el mismo que entrega
           physmem_tomar_marco justo despues. */
        int liberado = desalojar_victima(sis, st, &vpn_victima);

        if (liberado < 0) {
            return 0; /* sin memoria para guardar la victima en el swap */
        }
        acc->vpn_victima = (long) vpn_victima;

        marco = physmem_tomar_marco(sis->mem, mmu_vpn(va));
        if (marco != liberado) {
            /* Comprobacion real, no un assert: si el marco entregado no es el que
               se acaba de liberar, la memoria fisica y la politica se
               desincronizaron, y seguir daria estadisticas falsas. */
            return 0;
        }
    }

    if (pte->swapped) {
        /* La pagina ya estuvo en memoria y se desalojo sucia: pfn guarda su slot.
           La copia se trae al marco y el slot queda asociado al marco mientras la
           pagina este presente, porque sigue siendo una copia valida. */
        uint32_t slot = pte->pfn;

        swap_leer(sis->swap, slot, physmem_datos_marco(sis->mem, marco));
        physmem_fijar_slot_swap(sis->mem, marco, (long) slot);
        pte->swapped = 0;
        stats_registrar_lectura_swap(st);
        acc->desde_swap = 1;
    }
    /* Si no tenia copia, physmem_tomar_marco ya dejo el marco en ceros. */

    pte->pfn     = (unsigned int) marco;
    pte->present = 1;
    pte->dirty   = 0;
    stats_registrar_fallo(st);

    politica_al_cargar(marco); /* la politica anota que este marco se acaba de llenar */
    return 1;
}

/*
 * Desaloja la pagina que elija la politica y devuelve el marco que quedo libre,
 * o -1 si no hubo memoria para guardar la victima en el swap.
 *
 * Lo esencial: la PTE de la victima pasa a present = 0 pero SIGUE con valid = 1.
 * La pagina no deja de existir, solo deja de estar en memoria; si el proceso la
 * vuelve a tocar, sera otro fallo de pagina y no un acceso ilegal.
 */
static int desalojar_victima(sistema_memoria_t *sis, stats_t *st, uint32_t *vpn_victima)
{
    int      victima = politica_elegir_victima();
    uint32_t vpn     = physmem_vpn_de(sis->mem, victima);
    pte_t   *pte     = pagetable_buscar_pte(sis->dir, mmu_pt1_de_vpn(vpn), mmu_pt2_de_vpn(vpn));
    long     slot    = physmem_slot_swap(sis->mem, victima);

    /* El mapeo inverso del marco tiene que llevar a una PTE que apunte de vuelta
       a ese mismo marco; si no, las dos estructuras se desincronizaron. */
    assert(pte != NULL);
    assert(pte->valid && pte->present);
    assert((int) pte->pfn == victima);

    if (pte->dirty) {
        /* Pagina modificada: su contenido se escribe al swap antes de soltar el
           marco. Si ya tenia slot se sobrescribe; si no, se reserva uno. */
        if (slot < 0) {
            uint32_t nuevo;

            if (!swap_reservar(sis->swap, &nuevo)) {
                return -1;
            }
            slot = (long) nuevo;
        }
        swap_escribir(sis->swap, (uint32_t) slot,
                      physmem_datos_marco_const(sis->mem, victima));
        stats_registrar_desalojo_sucio(st);
    }
    /* Pagina limpia: si tiene slot, esa copia sigue vigente; si no tiene, nunca
       se escribio y en el proximo fallo se entrega otra vez en ceros. */

    pte->present  = 0;                         /* ya no esta en memoria...          */
    pte->swapped  = (slot >= 0) ? 1u : 0u;
    pte->pfn      = (slot >= 0) ? (unsigned int) slot : 0u; /* ...pfn guarda el slot */
    pte->accessed = 0;                         /* al volver, entra sin referencias  */
    pte->dirty    = 0;

    physmem_devolver_marco(sis->mem, victima);
    stats_registrar_reemplazo(st);
    *vpn_victima = vpn;
    return victima;
}

const char *acceso_nombre_resultado(resultado_acceso_t resultado)
{
    switch (resultado) {
    case ACCESO_HIT:            return "hit";
    case ACCESO_FALLO_RESUELTO: return "FALLO";
    case ACCESO_ILEGAL:         return "acceso ilegal";
    case ACCESO_NO_ALINEADO:    return "no alineado";
    case ACCESO_ERROR_INTERNO:  return "error interno";
    }
    return "desconocido";
}
