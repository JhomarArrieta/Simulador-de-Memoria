# Resultados medidos (fase 9)

Configuración: página de 4 KB, memoria física de 256 KB = **64 marcos**, espacio
virtual de 32 bits. Reproducible con `./simulador tests/<archivo> -p <lru|fifo>`.

## Las seis corridas

| Test | Política | Accesos | Fallos | Hit rate | Reemplazos | AMAT | Páginas distintas | Fallos mínimos |
|---|---|---|---|---|---|---|---|---|
| `t1_basico.txt`    | LRU  | 4   | 2   | 50,00 % | 0   | 5 000,10 µs | 2   | 2   |
| `t1_basico.txt`    | FIFO | 4   | 2   | 50,00 % | 0   | 5 000,10 µs | 2   | 2   |
| `t2_localidad.txt` | LRU  | 60  | 4   | 93,33 % | 0   | 666,77 µs   | 4   | 4   |
| `t2_localidad.txt` | FIFO | 60  | 4   | 93,33 % | 0   | 666,77 µs   | 4   | 4   |
| `t3_estres.txt`    | LRU  | 400 | 200 | 50,00 % | 136 | 5 000,10 µs | 100 | 100 |
| `t3_estres.txt`    | FIFO | 400 | 200 | 50,00 % | 136 | 5 000,10 µs | 100 | 100 |

AMAT = tiempo medio de acceso, con el modelo de 100 ns por acceso a memoria y 10 ms
por fallo de página (OSTEP cap. 22.1). Un acceso sin fallos costaría 0,1 µs.

"Fallos mínimos posibles" son los *compulsory misses*: la primera vez que se toca
una página, ninguna política puede evitar el fallo.

- En `t1` y `t2` las dos políticas alcanzan **el mínimo teórico**: ninguna política,
  ni siquiera la óptima de Belady, podría hacerlo mejor. La memoria nunca se llena
  (2 y 4 páginas en 64 marcos), así que no hay ni un reemplazo y la política no
  interviene.
- En `t3` los fallos son **el doble** del mínimo: la segunda pasada no aprovecha
  nada de la primera.

## t3_estres: barrido del tamaño de memoria

Las 100 páginas distintas ocupan exactamente 400 KB.

| Memoria | Marcos | Fallos | Hit rate | Reemplazos | `fallos - marcos` |
|---|---|---|---|---|---|
| 256 KB | 64  | 200 | 50,00 % | 136 | 136 |
| 384 KB | 96  | 200 | 50,00 % | 104 | 104 |
| 396 KB | 99  | 200 | 50,00 % | 101 | 101 |
| 400 KB | 100 | 100 | 75,00 % | 0   | —   |
| 512 KB | 128 | 100 | 75,00 % | 0   | —   |

Idéntico con LRU y con FIFO en las cinco configuraciones.

Con **99 marcos para 100 páginas** —un solo marco de menos— el resultado es igual
que con 64 marcos. En 100 marcos cae de golpe a los 100 fallos obligatorios. No hay
mejora gradual: es un acantilado, el comportamiento clásico del recorrido cíclico.

## Verificaciones

- `accesos = hits + fallos` en las 6 corridas (y en 28 combinaciones de archivo,
  política y tamaño de memoria).
- `reemplazos = fallos - marcos` siempre que la memoria se llena una vez y no se
  libera: 136 = 200 - 64, 104 = 200 - 96, 101 = 200 - 99.
- El primer reemplazo ocurre en el **fallo 65**, no antes: el fallo 64 recibe el
  marco 63 (el último libre) y el fallo 65 reutiliza el marco 0.
- Fallos por pasada en `t3`: 100 en la primera y 100 en la segunda.
