/*
 * config.h - parametros del modelo que se simula.
 *
 * Estan aparte porque tanto la tabla de paginas como la memoria fisica los
 * necesitan (TAM_PAGINA sobre todo), y no tiene sentido que un modulo incluya
 * al otro solo por una constante.
 */
#ifndef CONFIG_H
#define CONFIG_H

/*
 * Formato de la direccion virtual de 32 bits con la pagina por defecto de 4 KB:
 *
 *  31            22 21            12 11                 0
 * +----------------+----------------+-------------------+
 * |   PT1 (10 b)   |   PT2 (10 b)   |   offset (12 b)   |
 * +----------------+----------------+-------------------+
 *
 * El tamano de pagina se configura al compilar:  make PAGE_BITS=13  (8 KB).
 * Los otros dos campos se derivan de BITS_OFFSET para que siempre sumen 32:
 *
 *  - PT2 usa BITS_OFFSET - 2 bits: con PTEs de 4 bytes, una tabla de nivel 2
 *    ocupa exactamente una pagina, que es la regla de OSTEP cap. 20.3
 *    ("More Than Two Levels": cada pieza de la tabla debe caber en una pagina).
 *  - PT1 recibe los bits restantes: 32 - BITS_OFFSET - BITS_NIVEL2.
 *
 * Con 4 KB se obtiene 10 | 10 | 12, el formato que exige el enunciado.
 */
#ifndef BITS_OFFSET
#define BITS_OFFSET 12
#endif

#if BITS_OFFSET < 10 || BITS_OFFSET > 16
#error "BITS_OFFSET debe estar entre 10 (paginas de 1 KB) y 16 (paginas de 64 KB)"
#endif

#define BITS_DIRECCION 32
#define BITS_NIVEL2    (BITS_OFFSET - 2)
#define BITS_NIVEL1    (BITS_DIRECCION - BITS_OFFSET - BITS_NIVEL2)

/* La direccion fisica tambien es de 32 bits: el PFN ocupa lo que deja el offset. */
#define BITS_PFN       (BITS_DIRECCION - BITS_OFFSET)

#define TAM_PAGINA      (1u << BITS_OFFSET) /* 4096 bytes por defecto */
#define TAM_PALABRA     4u                  /* read y write trabajan con palabras de 32 bits */
#define ENTRADAS_NIVEL1 (1u << BITS_NIVEL1) /* 1024 con paginas de 4 KB */
#define ENTRADAS_NIVEL2 (1u << BITS_NIVEL2) /* 1024 con paginas de 4 KB */

/* Paginas virtuales posibles en el espacio de 32 bits: 2^20 = 1048576 con 4 KB. */
#define PAGINAS_VIRTUALES (1u << (BITS_NIVEL1 + BITS_NIVEL2))

/* Bytes que cubre una sola tabla de nivel 2: 1024 paginas * 4 KB = 4 MB con 4 KB. */
#define BYTES_POR_TABLA_NIVEL2 ((unsigned long) ENTRADAS_NIVEL2 * TAM_PAGINA)

/*
 * Modelo de tiempo simulado. Se usan los valores tipicos de OSTEP cap. 22.1: un
 * acceso a memoria cuesta ~100 ns y traer una pagina del disco ~10 ms. No son
 * inventados, y la diferencia de cinco ordenes de magnitud entre los dos es
 * justamente lo que hace que un fallo de pagina sea tan caro.
 */
#define TIEMPO_ACCESO_MEMORIA_NS 100.0
#define TIEMPO_FALLO_DISCO_NS    10000000.0 /* 10 ms */

/*
 * El enunciado exige memoria fisica configurable con un minimo de 256 KB. El
 * maximo de 1 GB es un limite practico del simulador (la memoria simulada se
 * reserva completa); queda por debajo de los 4 GB que direcciona una PA de 32
 * bits, asi que todo numero de marco cabe en los BITS_PFN de la PTE.
 */
#define MEMORIA_FISICA_KB_DEFECTO 256
#define MEMORIA_FISICA_KB_MINIMA  256
#define MEMORIA_FISICA_KB_MAXIMA  (1024u * 1024u)

#endif /* CONFIG_H */
