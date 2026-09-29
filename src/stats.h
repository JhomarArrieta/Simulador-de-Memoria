/*
 * stats.h - contadores del simulador.
 *
 * La invariante que sostiene todo el reporte es  accesos = hits + fallos.
 * Por eso los hits no se guardan: se derivan. Un contador que se puede
 * desincronizar con otro es un contador que en algun momento va a mentir.
 *
 * Un acceso ilegal (valid = 0) NO cuenta como acceso: nunca se completo, y si
 * entrara en el total ensuciaria el hit rate. Va en su propio contador.
 */
#ifndef STATS_H
#define STATS_H

typedef struct {
    unsigned long accesos;    /* lecturas + escrituras completadas */
    unsigned long lecturas;
    unsigned long escrituras;
    unsigned long fallos;     /* accesos que encontraron valid=1, present=0 */
    unsigned long reemplazos; /* desalojos por politica (fase 6)            */
    unsigned long ilegales;   /* accesos a paginas con valid=0              */

    /*
     * Desalojos de paginas con dirty = 1. No lo pide el enunciado: es material
     * para el analisis, porque cada uno seria una escritura a disco en un SO
     * real y es una diferencia medible entre politicas.
     */
    unsigned long desalojos_sucios;
} stats_t;

void stats_init(stats_t *s);

/* Un acceso que se completo. es_escritura != 0 para write. */
void stats_registrar_acceso(stats_t *s, int es_escritura);
void stats_registrar_fallo(stats_t *s);
void stats_registrar_reemplazo(stats_t *s);
void stats_registrar_ilegal(stats_t *s);
void stats_registrar_desalojo_sucio(stats_t *s);

unsigned long stats_hits(const stats_t *s);
double        stats_hit_rate(const stats_t *s); /* en porcentaje; 0 si no hubo accesos */

/*
 * Las cinco lineas de estadisticas finales que exige el enunciado, en ese orden.
 * El nombre de la politica llega como texto: stats no sabe cuales existen.
 */
void stats_imprimir_reporte(const stats_t *s, const char *politica);

/* Desglose adicional y la suma de control; solo se imprime en modo verboso. */
void stats_imprimir_detalle(const stats_t *s);

#endif /* STATS_H */
