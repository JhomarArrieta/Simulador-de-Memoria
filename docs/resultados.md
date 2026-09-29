# Resultados medidos

Configuración: página de 4 KB, memoria física de 256 KB = **64 marcos**, espacio
virtual de 32 bits. Reproducible con `./simulador tests/<archivo> -p <lru|fifo> -v`.
El análisis está en `REPORTE.md` §5 a §7.

## Las seis pruebas

| Prueba | Política | Accesos | Fallos | Hit rate | Reemplazos | Escrituras a swap | Lecturas de swap | AMAT |
|---|---|---|---|---|---|---|---|---|
| `t1_basico` | LRU | 4 | 2 | 50,00 % | 0 | 0 | 0 | 5 000,1 µs |
| `t1_basico` | FIFO | 4 | 2 | 50,00 % | 0 | 0 | 0 | 5 000,1 µs |
| `t2_localidad` | LRU | 60 | 4 | 93,33 % | 0 | 0 | 0 | 666,8 µs |
| `t2_localidad` | FIFO | 60 | 4 | 93,33 % | 0 | 0 | 0 | 666,8 µs |
| `t3_estres` | LRU | 400 | 200 | 50,00 % | 136 | 136 | 100 | 5 000,1 µs |
| `t3_estres` | FIFO | 400 | 200 | 50,00 % | 136 | 136 | 100 | 5 000,1 µs |
| `t4_localidad_8020` | LRU | 5 000 | 486 | 90,28 % | 422 | 202 | 304 | 972,1 µs |
| `t4_localidad_8020` | FIFO | 5 000 | 660 | 86,80 % | 596 | 396 | 487 | 1 320,1 µs |
| `t5_swap` | LRU | 160 | 160 | 0,00 % | 96 | 80 | 80 | 10 000,1 µs |
| `t5_swap` | FIFO | 160 | 160 | 0,00 % | 96 | 80 | 80 | 10 000,1 µs |
| `t6_discriminante` | LRU | 71 | 65 | 8,45 % | 1 | 1 | 0 | 9 155,0 µs |
| `t6_discriminante` | FIFO | 71 | 66 | 7,04 % | 2 | 2 | 1 | 9 295,9 µs |

AMAT con 100 ns por acceso a memoria y 10 ms por fallo (OSTEP cap. 22.1).

## Barrido de memoria: `t4_localidad_8020` (`make barrido`)

| Memoria | Marcos | Fallos LRU | Fallos FIFO | Hit rate LRU | Hit rate FIFO | Reemplazos LRU | Reemplazos FIFO |
|---|---:|---:|---:|---:|---:|---:|---:|
| 256 KB | 64 | 486 | 660 | 90,28 % | 86,80 % | 422 | 596 |
| 320 KB | 80 | 307 | 375 | 93,86 % | 92,50 % | 227 | 295 |
| 384 KB | 96 | 135 | 158 | 97,30 % | 96,84 % | 39 | 62 |
| 448 KB | 112 | 100 | 100 | 98,00 % | 98,00 % | 0 | 0 |

## Barrido de memoria: `estres_grande` (`make stress`, `make barrido TRAZA=tests/estres_grande.txt`)

| Memoria | Marcos | Fallos LRU | Fallos FIFO | Hit rate LRU | Hit rate FIFO |
|---|---:|---:|---:|---:|---:|
| 256 KB | 64 | 30 987 | 39 617 | 69,01 % | 60,38 % |
| 320 KB | 80 | 21 769 | 32 225 | 78,23 % | 67,78 % |
| 384 KB | 96 | 16 809 | 26 667 | 83,19 % | 73,33 % |
| 448 KB | 112 | 14 205 | 22 440 | 85,80 % | 77,56 % |
| 512 KB | 128 | 12 458 | 18 773 | 87,54 % | 81,23 % |
| 640 KB | 160 | 9 295 | 12 866 | 90,70 % | 87,13 % |
| 768 KB | 192 | 6 182 | 8 191 | 93,82 % | 91,81 % |
| 1024 KB | 256 | 256 | 256 | 99,74 % | 99,74 % |

## Barrido de memoria: `t3_estres` (igual con LRU y FIFO)

| Memoria | Marcos | Fallos | Hit rate | Reemplazos | `fallos - marcos` |
|---|---|---|---|---|---|
| 256 KB | 64 | 200 | 50,00 % | 136 | 136 |
| 384 KB | 96 | 200 | 50,00 % | 104 | 104 |
| 396 KB | 99 | 200 | 50,00 % | 101 | 101 |
| 400 KB | 100 | 100 | 75,00 % | 0 | — |
| 512 KB | 128 | 100 | 75,00 % | 0 | — |

## Tamaño de página (`t4_localidad_8020`, 256 KB, `make PAGE_BITS=n`)

| Página | Marcos | Memoria de traducción | Fallos LRU | Fallos FIFO |
|---|---|---|---|---|
| 1 KB | 256 | 133 240 B | 601 | 745 |
| 4 KB | 64 | 12 400 B | 486 | 660 |
| 16 KB | 16 | 17 008 B | 463 | 638 |
| 64 KB | 4 | 65 680 B | 540 | 701 |

## Verificaciones

- `accesos = hits + fallos` en las 12 corridas (`make test`).
- `reemplazos = fallos - marcos` siempre que la memoria se llena y no se libera:
  136 = 200 - 64, 422 = 486 - 64, 596 = 660 - 64.
- `make diferencial`: 400 corridas contra un modelo de referencia, 0 diferencias en
  fallos, reemplazos y valores leídos.
