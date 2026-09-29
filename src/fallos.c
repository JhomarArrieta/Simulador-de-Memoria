/*
 * fallos.c - manejo de fallos de pagina y ejecucion del acceso.
 */
#include "fallos.h"

#include "mmu.h"
#include "replace.h"

#include <assert.h>

static acceso_t ejecutar(directorio_t *dir, memoria_fisica_t *mem, stats_t *st,
                         uint32_t va, int es_escritura, unsigned char valor);
static int      desalojar_victima(directorio_t *dir, memoria_fisica_t *mem, stats_t *st);

acceso_t acceso_leer(directorio_t *dir, memoria_fisica_t *mem, stats_t *st, uint32_t va)
{
    return ejecutar(dir, mem, st, va, 0, 0);
}

acceso_t acceso_escribir(directorio_t *dir, memoria_fisica_t *mem, stats_t *st,
                         uint32_t va, unsigned char valor)
{
    return ejecutar(dir, mem, st, va, 1, valor);
}

/*
 * Un acceso, de principio a fin. El orden importa: primero se resuelve si la
 * pagina se puede usar, y solo cuando esta presente se toca la memoria y se
 * cuentan las estadisticas.
 */
static acceso_t ejecutar(directorio_t *dir, memoria_fisica_t *mem, stats_t *st,
                         uint32_t va, int es_escritura, unsigned char valor)
{
    acceso_t     acc;
    traduccion_t t;

    assert(dir != NULL && mem != NULL && st != NULL);

    acc.resultado = ACCESO_ILEGAL;
    acc.pa        = 0;
    acc.marco     = -1;
    acc.valor     = 0;

    t = mmu_traducir(dir, va);

    /*
     * valid = 0 (o sin tabla de nivel 2): la pagina nunca se asigno. Es un acceso
     * ilegal, el equivalente al segfault, y NO es un fallo de pagina: no se trae
     * nada a memoria y no cuenta como acceso, porque nunca se completo.
     */
    if (t.resultado == TRAD_SEGFAULT) {
        stats_registrar_ilegal(st);
        return acc;
    }

    /*
     * valid = 1, present = 0: la pagina existe pero no esta en un marco. Este si
     * es el fallo de pagina que hay que atender.
     */
    if (t.resultado == TRAD_FALLO_PAGINA) {
        int marco = physmem_tomar_marco(mem, mmu_vpn(va));

        if (marco < 0) {
            /* Memoria llena: la politica elige una victima, se invalida su PTE y
               su marco vuelve a la lista de libres. El marco que se acaba de
               liberar es la cabeza de esa lista, asi que es el mismo que entrega
               physmem_tomar_marco justo despues. */
            int liberado = desalojar_victima(dir, mem, st);

            marco = physmem_tomar_marco(mem, mmu_vpn(va));
            if (marco != liberado) {
                /*
                 * Comprobacion real, no un assert: si el marco entregado no es el
                 * que se acaba de liberar, la memoria fisica y la politica se
                 * desincronizaron, y seguir daria estadisticas falsas. Cubre
                 * tambien el caso marco < 0, porque liberado nunca es negativo.
                 */
                acc.resultado = ACCESO_SIN_MARCOS;
                return acc;
            }
        }

        assert(t.pte != NULL);
        t.pte->pfn     = (unsigned int) marco;
        t.pte->present = 1;
        stats_registrar_fallo(st);

        /* La PA que traia la traduccion no servia: la pagina no tenia marco. */
        t.pa          = mmu_armar_pa((uint32_t) marco, mmu_offset(va));
        acc.resultado = ACCESO_FALLO_RESUELTO;

        politica_al_cargar(marco); /* la politica anota que este marco se acaba de llenar */
    } else {
        assert(t.resultado == TRAD_OK);
        acc.resultado = ACCESO_HIT;
    }

    /* De aqui en adelante la pagina esta presente y el acceso se completa. */
    assert(t.pte != NULL && t.pte->valid && t.pte->present);

    t.pte->accessed = 1; /* referenciada: la usa el algoritmo del reloj de un SO real */
    if (es_escritura) {
        t.pte->dirty = 1; /* modificada: un SO real tendria que escribirla al disco */
        physmem_escribir_byte(mem, t.pa, valor);
        acc.valor = valor;
    } else {
        acc.valor = physmem_leer_byte(mem, t.pa);
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
 * Desaloja la pagina que elija la politica y devuelve el marco que quedo libre.
 *
 * Lo esencial: la PTE de la victima pasa a present = 0 pero SIGUE con valid = 1.
 * La pagina no deja de existir, solo deja de estar en memoria; si el proceso la
 * vuelve a tocar, sera otro fallo de pagina y no un acceso ilegal.
 */
static int desalojar_victima(directorio_t *dir, memoria_fisica_t *mem, stats_t *st)
{
    int      victima = politica_elegir_victima();
    uint32_t vpn     = physmem_vpn_de(mem, victima);
    pte_t   *pte     = pagetable_buscar_pte(dir, mmu_pt1_de_vpn(vpn), mmu_pt2_de_vpn(vpn));

    /* El mapeo inverso del marco tiene que llevar a una PTE que apunte de vuelta
       a ese mismo marco; si no, las dos estructuras se desincronizaron. */
    assert(pte != NULL);
    assert(pte->valid && pte->present);
    assert((int) pte->pfn == victima);

    if (pte->dirty) {
        /* Un SO real tendria que escribir esta pagina al disco antes de soltar el
           marco. Este simulador no tiene area de intercambio, asi que el
           contenido se pierde: es una limitacion del modelo, no un bug. */
        stats_registrar_desalojo_sucio(st);
    }

    pte->present  = 0; /* ya no esta en memoria... */
    pte->pfn      = 0; /* ...y el marco dejo de ser suyo */
    pte->accessed = 0; /* al volver, entra sin referencias y limpia */
    pte->dirty    = 0;

    physmem_devolver_marco(mem, victima);
    stats_registrar_reemplazo(st);
    return victima;
}

const char *acceso_nombre_resultado(resultado_acceso_t resultado)
{
    switch (resultado) {
    case ACCESO_HIT:            return "hit";
    case ACCESO_FALLO_RESUELTO: return "FALLO";
    case ACCESO_ILEGAL:         return "acceso ilegal";
    case ACCESO_SIN_MARCOS:     return "sin marcos libres";
    }
    return "desconocido";
}
