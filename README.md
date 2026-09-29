# Simulador de memoria virtual con paginación de dos niveles

Laboratorio 2 de Sistemas Operativos — Universidad de Antioquia, 2026-2.

Simulador de gestión de memoria virtual basado en paginación: traduce direcciones
virtuales de 32 bits a direcciones físicas usando una tabla de páginas de dos
niveles, atiende fallos de página y aplica una política de reemplazo (**LRU** por
defecto, **FIFO** disponible con un flag).

No reserva memoria real del sistema operativo para el proceso simulado: **modela**
un espacio virtual, una memoria física, un área de swap y sus tablas de páginas con
estructuras de datos en C.

## Requisitos

- `gcc` con soporte de C99 (probado con gcc 13.3 y clang 18)
- `make`
- `valgrind` (opcional, para `make valgrind`)
- `python3` (opcional, para `make diferencial`)

En WSL/Ubuntu: `sudo apt install -y build-essential valgrind python3`.

## Compilación

```bash
make          # compila y deja el binario ./simulador
make clean    # borra build/ y el binario
make run      # compila y corre tests/t1_basico.txt
```

`make run` acepta otro archivo sin editar el Makefile:

```bash
make run ENTRADA=tests/t3_estres.txt
make run ENTRADA=tests/t2_localidad.txt ARGS="-p fifo"
```

### Tamaño de página

El tamaño de página es 4 KB por defecto y se elige al compilar, entre 1 KB y 64 KB:

```bash
make PAGE_BITS=13     # paginas de 8 KB: VA = PT1 8 b | PT2 11 b | offset 13 b
make                  # vuelve a 4 KB (PAGE_BITS=12); al cambiar la pagina se recompila todo
```

PT2 usa `PAGE_BITS − 2` bits (una tabla de nivel 2 ocupa una página, OSTEP cap. 20.3)
y PT1 los bits restantes, así que la dirección virtual siempre suma 32 bits.

### Sin warnings

El proyecto compila **sin un solo warning** con el comando exigido por la rúbrica:

```bash
gcc -Wall -Werror -std=c99 -o simulador src/*.c
```

## Uso

```
./simulador <archivo_entrada> [-p lru|fifo] [-m <KB_memoria_fisica>] [-v]
```

| Opción | Descripción | Por defecto |
|---|---|---|
| `-p lru` / `-p fifo` | Política de reemplazo | `lru` |
| `-m <KB>` | Tamaño de la memoria física en KB (mínimo 256, máximo 1 048 576, múltiplo de la página) | `256` |
| `-v` | Traza cada comando y muestra el tamaño de las estructuras | desactivado |

Las opciones van en cualquier orden. `-v` no está en el enunciado: es una ayuda
para verificar la traducción paso a paso, y no altera la salida obligatoria.

### Archivo de entrada

Los comandos se leen como un flujo de palabras separadas por espacios, tabs o
saltos de línea, así que se aceptan los dos formatos: un comando por línea, o
varios comandos en la misma línea, como aparece el ejemplo en el enunciado.
`#` inicia un comentario que termina en el salto de línea.

| Comando | Efecto |
|---|---|
| `alloc <bytes>` | Asigna espacio virtual, redondeado a páginas completas |
| `write <dir_virtual> <valor>` | Escribe una palabra de 32 bits en esa dirección |
| `read <dir_virtual>` | Lee la palabra de 32 bits de esa dirección |
| `free <dir_virtual>` | Libera la asignación que empieza en esa dirección |

Las direcciones y los valores se escriben en decimal (`4096`) o en hexadecimal con
prefijo (`0x1000`).

```
# un comando por linea
alloc 8192
write 0 42
write 4096 99
read 0
read 4096
```

```
# todo en una linea, como en el PDF del enunciado
alloc 8192 write 0 42 write 4096 99 read 0 read 4096
```

Un comando mal formado (verbo desconocido, argumento faltante o no numérico) se
reporta por `stderr` con su número de línea y se ignora; los demás se ejecutan.

### Salida

Por defecto, solo las estadísticas finales que pide el enunciado:

```
$ ./simulador tests/t2_localidad.txt
Total de accesos: 60
Total fallos de página: 4
Hit rate: 93.33%
Total reemplazos: 0
Política: LRU
Tiempo de ejecución (CPU): 0.039 ms
Tiempo simulado: 40.006 ms (100 ns por acceso, 10 ms por fallo)
Tiempo medio de acceso (AMAT): 666.767 µs
```

Las cinco primeras líneas son el bloque que especifica el enunciado. Las tres de
tiempo cumplen su requisito funcional 4 ("registrar estadísticas: número de fallos,
número de reemplazos, tiempo") y van después para no alterar ese bloque:

- **Tiempo de ejecución (CPU)**: lo que tardó el simulador en esta máquina, medido
  con `clock()`. Solo dice qué tan rápido es el simulador.
- **Tiempo simulado**: lo que habría tardado el programa simulado, cobrando 100 ns
  por acceso a memoria y 10 ms por traer una página del disco (los valores típicos
  de OSTEP cap. 22.1). Es lo que mide el efecto de la memoria virtual.
- **AMAT**: tiempo medio por acceso. Con 93,33 % de aciertos el promedio es de
  666 µs, unas 6 600 veces más lento que un acceso a memoria: un recordatorio de lo
  caro que es un fallo de página.

Los errores y avisos (accesos ilegales o no alineados, comandos mal formados)
salen por `stderr`, así que `./simulador entrada.txt 2>/dev/null` deja solo el
bloque de estadísticas.

Con `-v` se agregan la configuración, la traza por comando y un detalle con la
suma de control:

```
$ ./simulador tests/t1_basico.txt -v
...
[linea   3] write 0x00000000 = 42         -> FALLO marco  0, PA 0x00000000
[linea   5] read  0x00000000 = 42         -> hit   marco  0, PA 0x00000000
...
suma de control: hits + fallos = 4 == accesos = 4
```

### Código de salida

`0` si todo se ejecutó; `1` si hubo líneas mal formadas o comandos que no se
pudieron ejecutar. Un acceso ilegal del programa simulado **no** cambia el código
de salida: es una falla del programa de prueba, no del simulador, y se reporta en
las estadísticas.

## Modelo simulado

Dirección virtual de 32 bits, página de 4 KB:

```
 31            22 21            12 11                 0
+----------------+----------------+-------------------+
|   PT1 (10 b)   |   PT2 (10 b)   |   offset (12 b)   |
+----------------+----------------+-------------------+
```

- **PT1** (bits 31-22): índice en el directorio, 1024 entradas.
- **PT2** (bits 21-12): índice en la tabla de nivel 2, 1024 entradas.
- **offset** (bits 11-0): desplazamiento dentro de la página. Nunca cambia en la
  traducción: `pa = (pfn << 12) | offset`.

Las tablas de nivel 2 se crean cuando un `alloc` toca su región de 4 MB y se
liberan cuando se quedan sin páginas válidas (ver `REPORTE.md` §3.2).

Un fallo de página toma un marco libre o, si no hay, desaloja la víctima que elige
la política. Si la víctima está sucia se escribe en el área de swap; cuando la
página vuelve a usarse, el fallo la trae desde el swap. Mientras una página está
fuera de memoria, el campo `pfn` de su PTE guarda su slot de swap (OSTEP cap. 21.3).

## Estructura del proyecto

```
src/main.c          linea de comandos, ciclo principal, ejecucion de comandos
src/parser.c/.h     lectura de comandos como flujo de tokens
src/config.h        parametros del modelo (pagina, formato de la VA, limites)
src/mmu.c/.h        extraccion de PT1/PT2/offset y traduccion VA->PA
src/pagetable.c/.h  directorio, tablas de nivel 2, PTEs, alloc y free
src/physmem.c/.h    marcos, contenido, lista de marcos libres
src/swap.c/.h       area de swap: slots con copias de paginas desalojadas
src/fallos.c/.h     ejecucion del acceso y manejo del fallo de pagina
src/replace.c/.h    interfaz de politica + implementaciones LRU y FIFO
src/stats.c/.h      contadores y reporte final
tests/              programas de prueba y scripts (verificar, barrido, diferencial)
docs/lab-spec.md    enunciado del laboratorio
docs/resultados.md  tabla de resultados medidos
REPORTE.md          reporte de analisis
```

## Pruebas

| Archivo | Qué prueba |
|---|---|
| `tests/t1_basico.txt` | El ejemplo del enunciado: traducción y fallos obligatorios |
| `tests/t2_localidad.txt` | 60 accesos sobre 4 páginas: localidad temporal y espacial |
| `tests/t3_estres.txt` | Barrido cíclico de 100 páginas en 64 marcos: peor caso de LRU y FIFO |
| `tests/t4_localidad_8020.txt` | 5000 accesos 80/20 sobre 100 páginas: donde LRU supera a FIFO |
| `tests/t5_swap.txt` | 80 páginas escritas y releídas: los datos sobreviven al desalojo |
| `tests/t6_discriminante.txt` | Página antigua pero caliente: LRU 65 fallos, FIFO 66 |

`t4`, `t5` y `t6` se regeneran con `bash tests/generar_pruebas.sh`. El generador
de números pseudoaleatorios es propio, así que la traza sale idéntica en cualquier
máquina.

```bash
make test       # verificaciones automáticas (valores leídos, swap, políticas, invariantes)
make valgrind   # valgrind sobre las 6 pruebas con LRU y con FIFO
make stress     # genera tests/estres_grande.txt (100000 accesos 80/20) y lo ejecuta
make barrido    # tabla y gráfico de fallos contra tamaño de memoria (t4 por defecto)
make barrido TRAZA=tests/t3_estres.txt
```

Resultados medidos y su análisis: `docs/resultados.md` y `REPORTE.md`.

## Verificación de calidad

```bash
gcc -Wall -Werror -std=c99 -o simulador src/*.c        # sin warnings
make valgrind
```

```
valgrind OK: tests/t1_basico.txt -p lru
...
valgrind OK: tests/t6_discriminante.txt -p fifo
```

## Limitaciones conocidas

Son decisiones de modelado y están explicadas en `REPORTE.md` §8:

1. **El tamaño de página se elige al compilar** (`make PAGE_BITS=n`), no con un flag.
2. **Palabras de 32 bits alineadas.** `read` y `write` usan palabras de 4 bytes en
   direcciones múltiplo de 4; una dirección no alineada se rechaza y no cuenta como
   acceso.
3. **El espacio virtual no se reutiliza.** `alloc` reparte desde la dirección 0 con
   un puntero que solo sube, como `brk`/`sbrk`: tras un `free`, el siguiente
   `alloc` no rellena el hueco.
4. **`free` exige la dirección exacta** que devolvió un `alloc`, igual que
   `free(ptr)` en C. Una dirección al medio de la asignación se rechaza.
5. **La página 0 es válida.** Un SO real la deja sin mapear para que
   desreferenciar `NULL` falle; aquí el enunciado la usa como dirección de trabajo.
6. **El swap vive en la memoria del simulador** y no tiene límite de tamaño.

## Uso de herramientas de inteligencia artificial

Este proyecto se desarrolló con asistencia de Claude Code (Anthropic), como lo
registra el historial de commits, y la revisión final contra la rúbrica se hizo con
asistencia de Claude. El detalle está en `REPORTE.md` §12.
