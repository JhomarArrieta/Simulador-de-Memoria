# Simulador de memoria virtual con paginación de dos niveles

Laboratorio 2 de Sistemas Operativos — Universidad de Antioquia, 2026-2.

Simulador de gestión de memoria virtual basado en paginación: traduce direcciones
virtuales de 32 bits a direcciones físicas usando una tabla de páginas de dos
niveles, atiende fallos de página y aplica una política de reemplazo (**LRU** por
defecto, **FIFO** disponible con un flag).

No reserva memoria real del sistema operativo para el proceso simulado: **modela**
un espacio virtual, una memoria física y sus tablas de páginas con estructuras de
datos en C.

## Requisitos

- `gcc` con soporte de C99 (probado con gcc 16 y clang 22)
- `make`
- `valgrind` (opcional, solo para verificar que no hay fugas)

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
| `-m <KB>` | Tamaño de la memoria física en KB (mínimo 256) | `256` |
| `-v` | Traza cada comando y muestra el tamaño de las estructuras | desactivado |

Las opciones van en cualquier orden. `-v` no está en el enunciado: es una ayuda
para verificar la traducción paso a paso, y no altera la salida obligatoria.

### Archivo de entrada

Un comando por línea. Las líneas vacías y las que empiezan con `#` se ignoran, y
`#` también sirve para comentar al final de una línea.

| Comando | Efecto |
|---|---|
| `alloc <bytes>` | Asigna espacio virtual, redondeado a páginas completas |
| `write <dir_virtual> <valor>` | Escribe un byte en esa dirección |
| `read <dir_virtual>` | Lee el byte de esa dirección |
| `free <dir_virtual>` | Libera la asignación que empieza en esa dirección |

Las direcciones y los valores se escriben en decimal (`4096`) o en hexadecimal con
prefijo (`0x1000`).

```
# ejemplo
alloc 8192
write 0 42
write 4096 99
read 0
read 4096
```

### Salida

Por defecto, solo las estadísticas finales que pide el enunciado:

```
$ ./simulador tests/t2_localidad.txt
Total de accesos: 60
Total fallos de página: 4
Hit rate: 93.33%
Total reemplazos: 0
Política: LRU
```

Los errores y avisos (accesos ilegales, líneas mal formadas, valores truncados)
salen por `stderr`, así que `./simulador entrada.txt 2>/dev/null` deja solo el
bloque de estadísticas.

Con `-v` se agregan la configuración, la traza por comando y un detalle con la
suma de control:

```
$ ./simulador tests/t1_basico.txt -v
...
[linea   3] write 0x00000000 = 42  -> FALLO marco  0, PA 0x00000000
[linea   5] read  0x00000000 = 42  -> hit   marco  0, PA 0x00000000
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

Las tablas de nivel 2 se crean bajo demanda y se liberan cuando se quedan sin
páginas válidas.

## Estructura del proyecto

```
src/main.c          linea de comandos, ciclo principal, ejecucion de comandos
src/parser.c/.h     parseo de las lineas del archivo de entrada
src/config.h        parametros del modelo (pagina, entradas, memoria minima)
src/mmu.c/.h        extraccion de PT1/PT2/offset y traduccion VA->PA
src/pagetable.c/.h  directorio, tablas de nivel 2, PTEs, alloc y free
src/physmem.c/.h    marcos, contenido, lista de marcos libres
src/fallos.c/.h     ejecucion del acceso y manejo del fallo de pagina
src/replace.c/.h    interfaz de politica + implementaciones LRU y FIFO
src/stats.c/.h      contadores y reporte final
tests/              programas de prueba
docs/lab-spec.md    enunciado del laboratorio
docs/resultados.md  tabla de resultados medidos
REPORTE.md          reporte de analisis
```

## Pruebas

```bash
for t in tests/*.txt; do
  for p in lru fifo; do
    echo "== $t -p $p"; ./simulador "$t" -p "$p" 2>/dev/null
  done
done
```

Resultados medidos y su análisis: `docs/resultados.md` y `REPORTE.md`.

## Verificación de calidad

```bash
gcc -Wall -Werror -std=c99 -o simulador src/*.c        # sin warnings
valgrind --leak-check=full ./simulador tests/t3_estres.txt
```

```
All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors from 0 contexts
```

## Limitaciones conocidas

Son decisiones de modelado, no defectos, y están explicadas en `REPORTE.md`:

1. **No hay área de intercambio (swap).** Una página desalojada pierde su
   contenido; al volver, su marco llega en ceros. El simulador cuenta cuántos
   desalojos habrían exigido escribir a disco (páginas con `dirty = 1`).
2. **Un byte por dirección.** `write` guarda un byte; un valor mayor que 255 se
   trunca avisando por `stderr`.
3. **El espacio virtual no se reutiliza.** `alloc` reparte desde la dirección 0 con
   un puntero que solo sube, como `brk`/`sbrk`: tras un `free`, el siguiente
   `alloc` no rellena el hueco.
4. **`free` exige la dirección exacta** que devolvió un `alloc`, igual que
   `free(ptr)` en C. Una dirección al medio de la asignación se rechaza.
5. **La página 0 es válida.** Un SO real la deja sin mapear para que
   desreferenciar `NULL` falle; aquí el enunciado la usa como dirección de trabajo.
