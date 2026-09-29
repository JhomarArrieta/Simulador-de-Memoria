/*
 * replace.c - politicas de reemplazo: FIFO y LRU.
 *
 * Las dos comparten una sola estructura: un arreglo de marcas y un contador que
 * crece con cada evento. La victima es siempre el marco con la marca mas pequena.
 * Lo unico que cambia es CUANDO se actualiza la marca:
 *
 *   FIFO: solo al cargar la pagina  -> marca = orden de llegada, y un acierto no
 *         reordena nada: sale la pagina que lleva mas tiempo en memoria.
 *   LRU : al cargar Y en cada acceso -> marca = ultimo uso, y sale la pagina que
 *         se uso hace mas tiempo.
 *
 * Esa es toda la diferencia, y esta contenida en politica_al_acceder.
 *
 * Costo: O(1) al cargar y al acceder, O(num_marcos) al elegir victima. Con 64
 * marcos ese recorrido es irrelevante, y solo ocurre en un desalojo, no en cada
 * acceso. Memoria: 8 bytes por marco (512 B con 64 marcos).
 *
 * Se eligio el arreglo de marcas y no una cola enlazada por un caso concreto: la
 * interfaz no tiene un politica_al_liberar, asi que cuando free devuelve un
 * marco la politica no se entera. Con marcas eso se resuelve solo, porque al
 * reusar el marco politica_al_cargar sobrescribe su marca. Una cola enlazada
 * dejaria ese marco duplicado y acabaria desalojando un marco que ya no le
 * corresponde a esa pagina.
 */
#include "replace.h"

#include <assert.h>
#include <stdlib.h>

static politica_t     politica_activa = POLITICA_LRU; /* el defecto del laboratorio */
static unsigned long *marca           = NULL; /* 0 = el marco nunca recibio una pagina */
static int            num_marcos_total = 0;
static unsigned long  contador        = 0;

int politica_init(politica_t p, int num_marcos)
{
    assert(num_marcos > 0);
    assert(p == POLITICA_LRU || p == POLITICA_FIFO);

    politica_liberar(); /* por si se llama dos veces */

    marca = calloc((size_t) num_marcos, sizeof(unsigned long));
    if (marca == NULL) {
        return 0;
    }

    politica_activa  = p;
    num_marcos_total = num_marcos;
    contador         = 0;
    return 1;
}

void politica_al_cargar(int marco)
{
    assert(marca != NULL);
    assert(marco >= 0 && marco < num_marcos_total);

    /*
     * Las dos politicas marcan la carga: una pagina que acaba de entrar es la mas
     * reciente en llegar (FIFO) y tambien la mas recientemente usada (LRU).
     * Pre-incremento: la marca de un marco cargado siempre es >= 1, asi el 0
     * sigue significando "este marco nunca tuvo una pagina".
     */
    marca[marco] = ++contador;
}

void politica_al_acceder(int marco)
{
    assert(marca != NULL);
    assert(marco >= 0 && marco < num_marcos_total);

    /*
     * Aqui esta la unica diferencia entre las dos politicas.
     *
     * LRU actualiza el ultimo uso en CADA acceso, incluidos los aciertos: si no
     * lo hiciera en los hits, una pagina muy usada se veria igual de vieja que
     * una que solo se toco al cargarla, y el "recently used" no significaria
     * nada. Ese es el error clasico al implementar LRU.
     *
     * FIFO no hace nada: un acierto no reordena la cola, la pagina sale por
     * antiguedad sin importar cuanto se use.
     *
     * El if esta dentro de replace.c a proposito: es el unico lugar del programa
     * donde se puede preguntar que politica esta activa.
     */
    if (politica_activa == POLITICA_LRU) {
        marca[marco] = ++contador;
    }
}

int politica_elegir_victima(void)
{
    int i;
    int victima = 0;

    assert(marca != NULL && num_marcos_total > 0);

    /*
     * El minimo de las marcas: en FIFO la pagina que lleva mas tiempo en memoria,
     * en LRU la que se uso hace mas tiempo. O(num_marcos), pero solo en un
     * desalojo. Con 64 marcos no hace falta una lista enlazada; en un SO real, en
     * cambio, LRU exacto es impagable porque habria que actualizar estructuras en
     * cada acceso a memoria, y por eso se aproxima con el algoritmo del reloj y
     * el bit de referencia (OSTEP cap. 22.8).
     */
    for (i = 1; i < num_marcos_total; i++) {
        if (marca[i] < marca[victima]) {
            victima = i;
        }
    }

    /* Si la victima nunca recibio una pagina, alguien pidio un desalojo con
       marcos libres disponibles: eso seria un bug del manejador de fallos. */
    assert(marca[victima] != 0);
    return victima;
}

void politica_liberar(void)
{
    free(marca);
    marca            = NULL;
    num_marcos_total = 0;
    contador         = 0;
}

const char *politica_nombre(void)
{
    switch (politica_activa) {
    case POLITICA_FIFO: return "FIFO";
    case POLITICA_LRU:  return "LRU";
    }
    return "desconocida";
}
