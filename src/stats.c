/*
 * stats.c - contadores. Deliberadamente tonto: la logica esta en quien los llama.
 */
#include "stats.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void stats_init(stats_t *s)
{
    assert(s != NULL);
    memset(s, 0, sizeof(*s));
}

void stats_registrar_acceso(stats_t *s, int es_escritura)
{
    assert(s != NULL);
    s->accesos++;
    if (es_escritura) {
        s->escrituras++;
    } else {
        s->lecturas++;
    }
}

void stats_registrar_fallo(stats_t *s)
{
    assert(s != NULL);
    s->fallos++;
}

void stats_registrar_reemplazo(stats_t *s)
{
    assert(s != NULL);
    s->reemplazos++;
}

void stats_registrar_ilegal(stats_t *s)
{
    assert(s != NULL);
    s->ilegales++;
}

void stats_registrar_desalojo_sucio(stats_t *s)
{
    assert(s != NULL);
    s->desalojos_sucios++;
}

unsigned long stats_hits(const stats_t *s)
{
    assert(s != NULL);
    assert(s->fallos <= s->accesos); /* un fallo siempre termina en un acceso */
    return s->accesos - s->fallos;
}

double stats_hit_rate(const stats_t *s)
{
    unsigned long hits;

    assert(s != NULL);
    if (s->accesos == 0) {
        return 0.0; /* sin accesos no hay tasa que reportar; evita dividir por cero */
    }

    /* La conversion a double se hace sobre una variable y no sobre la llamada
       misma, que es lo que quiere -Wbad-function-cast y se lee mejor. */
    hits = stats_hits(s);
    return (double) hits * 100.0 / (double) s->accesos;
}

/*
 * Formato exacto de docs/lab-spec.md. Las tildes van aqui a proposito: este es el
 * bloque que se compara contra el enunciado, a diferencia de los mensajes de
 * diagnostico, que son ASCII.
 */
void stats_imprimir_reporte(const stats_t *s, const char *politica)
{
    assert(s != NULL && politica != NULL);

    printf("Total de accesos: %lu\n", s->accesos);
    printf("Total fallos de página: %lu\n", s->fallos);
    printf("Hit rate: %.2f%%\n", stats_hit_rate(s));
    printf("Total reemplazos: %lu\n", s->reemplazos);
    printf("Política: %s\n", politica);
}

void stats_imprimir_detalle(const stats_t *s)
{
    unsigned long hits = stats_hits(s);

    assert(s != NULL);

    printf("--- detalle ---\n");
    printf("lecturas / escrituras        : %lu / %lu\n", s->lecturas, s->escrituras);
    printf("hits                         : %lu\n", hits);
    printf("accesos ilegales (valid=0)   : %lu  (no cuentan como acceso)\n", s->ilegales);
    printf("desalojos con la pagina sucia: %lu  (escrituras a disco en un SO real)\n",
           s->desalojos_sucios);

    /* Suma de control del enunciado: si esto falla, algun contador miente. */
    printf("suma de control: hits + fallos = %lu %s accesos = %lu\n",
           hits + s->fallos,
           (hits + s->fallos == s->accesos) ? "==" : "!=",
           s->accesos);
}
