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
 * Formato de la direccion virtual de 32 bits:
 *
 *  31            22 21            12 11                 0
 * +----------------+----------------+-------------------+
 * |   PT1 (10 b)   |   PT2 (10 b)   |   offset (12 b)   |
 * +----------------+----------------+-------------------+
 */
#define BITS_NIVEL1 10
#define BITS_NIVEL2 10
#define BITS_OFFSET 12

#define TAM_PAGINA      (1u << BITS_OFFSET) /* 4096 bytes */
#define ENTRADAS_NIVEL1 (1u << BITS_NIVEL1) /* 1024 entradas en el directorio */
#define ENTRADAS_NIVEL2 (1u << BITS_NIVEL2) /* 1024 entradas por tabla de nivel 2 */

/* Paginas virtuales posibles en el espacio de 32 bits: 2^20 = 1048576. */
#define PAGINAS_VIRTUALES (1u << (BITS_NIVEL1 + BITS_NIVEL2))

/* Bytes que cubre una sola tabla de nivel 2: 1024 paginas * 4 KB = 4 MB. */
#define BYTES_POR_TABLA_NIVEL2 ((unsigned long) ENTRADAS_NIVEL2 * TAM_PAGINA)

/* El enunciado exige memoria fisica configurable con un minimo de 256 KB. */
#define MEMORIA_FISICA_KB_DEFECTO 256
#define MEMORIA_FISICA_KB_MINIMA  256

#endif /* CONFIG_H */
