# Simulador de memoria virtual con paginación de dos niveles

Contexto permanente del proyecto. Léelo antes de escribir o modificar código.

## Qué es esto

Laboratorio del curso de Sistemas Operativos (UdeA, 2026-2, prof. Juan Felipe Gallo).
Texto guía: *Operating Systems: Three Easy Pieces* (OSTEP), capítulos 18 (Paging),
19 (TLB) y 20 (Advanced Page Tables).

Es un **simulador**: no reserva memoria real del sistema operativo para el proceso
simulado, sino que **modela** un espacio virtual, una memoria física y sus tablas de
páginas con estructuras de datos en C.

La especificación completa está en `docs/lab-spec.md`. **Si algo de este archivo
contradice la especificación, manda la especificación** y avísame.

## Reglas de trabajo

- Respóndeme siempre en español.
- Este es un trabajo de curso: **explícame lo que haces**. Antes de implementar una
  función nueva, dime en dos o tres líneas qué va a hacer y por qué.
- **No escribas el laboratorio completo de una sola vez.** Vamos por fases (ver abajo),
  y cada fase debe compilar y correr antes de pasar a la siguiente.
- Si una decisión de diseño puede tomarse de varias formas, **pregúntame** en vez de
  asumir. Ejemplo: qué hacer con un `read` a una dirección que nunca se asignó.
- No inventes requisitos que no estén en `docs/lab-spec.md`.
- Comenta el código en español, explicando el *por qué*, no el *qué*.
- Cuando termines una fase, propón el mensaje de commit; yo decido si commiteo.

## Restricciones técnicas (de la rúbrica, no negociables)

- Lenguaje **C**, estándar **C99**.
- Debe compilar **sin un solo warning** con: `gcc -Wall -Werror -std=c99`.
- `Makefile` con targets: `all`, `clean`, `run`.
- **Sin memory leaks**: todo lo que se hace con `malloc`/`calloc` se libera. Debe pasar
  `valgrind --leak-check=full` sin bloques perdidos.
- Funciones separadas por responsabilidad: traducción, manejo de fallos, reemplazo,
  estadísticas, parseo de entrada.
- Nada de variables globales salvo que lo justifiquemos.

## Modelo que se simula

### Dirección virtual de 32 bits

```
 31            22 21            12 11                 0
+----------------+----------------+-------------------+
|   PT1 (10 b)   |   PT2 (10 b)   |   offset (12 b)   |
+----------------+----------------+-------------------+
```

- **PT1** (bits 31-22): índice en la tabla de nivel 1 (directorio), 1024 entradas.
- **PT2** (bits 21-12): índice en la tabla de nivel 2, 1024 entradas.
- **offset** (bits 11-0): desplazamiento dentro de la página de 4 KB.

Extracción con máscaras y corrimientos:

```c
pt1    = (va >> 22) & 0x3FF;   /* 10 bits */
pt2    = (va >> 12) & 0x3FF;   /* 10 bits */
offset =  va        & 0xFFF;   /* 12 bits */
```

La dirección física se arma igual que en clase: **el offset nunca cambia**.

```c
pa = (pfn << 12) | offset;     /* equivale a pfn * TAM_PAGINA + offset */
```

### Parámetros

- Tamaño de página: **4 KB** (configurable por constante o por parámetro).
- Espacio virtual: **32 bits** = 4 GB, con 2^20 páginas virtuales posibles.
- Memoria física: **configurable, mínimo 256 KB** → 256 KB / 4 KB = **64 marcos**.

### Tabla de dos niveles

El directorio (nivel 1) existe desde el inicio; **las tablas de nivel 2 se crean
dinámicamente** la primera vez que se toca esa región del espacio virtual. Esa es
justamente la ventaja de la tabla multinivel: el espacio virtual no usado no ocupa
memoria (OSTEP cap. 20).

### PTE (entrada de tabla de nivel 2)

Debe contener, como mínimo:

- número de marco físico (**PFN**)
- bit **valid**: la página fue asignada por el proceso
- bit **present**: la página está en memoria física (si está en 0, hay fallo de página)
- bit **accessed** (referencia)
- bit **dirty** (modificada por un `write`)

Ojo con la diferencia, que es tema de parcial: `valid = 0` significa acceso ilegal
(segmentation fault); `valid = 1, present = 0` significa que la página existe pero no
está en memoria → **fallo de página**.

## Entrada y salida

El programa recibe un archivo con comandos, uno por línea:

```
alloc <bytes>
write <virtual_addr> <value>
read  <virtual_addr>
free  <virtual_addr>
```

Salida final obligatoria:

- Total de accesos: N
- Total de fallos de página: M
- Hit rate: (N − M) / N × 100 %
- Total de reemplazos: K
- Política: FIFO o LRU

## Política de reemplazo: se implementan las dos

El profesor dejó escoger. La decisión del grupo es:

- **LRU es la política principal** del laboratorio (la que se declara en el reporte y
  la que corre por defecto).
- **FIFO se implementa también**, como política adicional. Esto apunta al bonus de la
  rúbrica (+10% por políticas adicionales) y, sobre todo, permite **comparar con datos
  medidos** en vez de solo con teoría, que es lo que pide el reporte.

### Cómo funciona cada una

- **FIFO**: sale la página que lleva más tiempo en memoria, sin importar cuánto se use.
  Cola simple: entra por atrás, sale por adelante. **Un acierto no reordena la cola.**
- **LRU**: sale la página usada hace más tiempo. Hay que actualizar la información de
  uso **en cada acceso, incluidos los aciertos**. Ese es el error clásico: olvidar
  actualizar en los hits.

### Interfaz obligatoria

La política va **detrás de una interfaz**, para poder cambiarla sin tocar el resto del
simulador. En `replace.h`, algo equivalente a:

```c
typedef enum { POLITICA_LRU, POLITICA_FIFO } politica_t;

void  politica_init(politica_t p, int num_marcos);
void  politica_al_cargar(int marco);   /* se llama cuando una pagina entra a un marco */
void  politica_al_acceder(int marco);  /* se llama en CADA acceso, hit o miss */
int   politica_elegir_victima(void);   /* devuelve el marco a desalojar */
void  politica_liberar(void);
```

Diferencia clave entre las dos implementaciones:

- En **FIFO**, `politica_al_acceder` **no hace nada**.
- En **LRU**, `politica_al_acceder` actualiza el último uso del marco.

El resto del código llama siempre a las mismas funciones y nunca pregunta qué política
está activa. Si aparece un `if (politica == ...)` fuera de `replace.c`, está mal.

### Implementación sugerida

- **LRU**: un contador global de accesos que se incrementa en cada acceso, y un arreglo
  `ultimo_uso[num_marcos]` donde se guarda el valor de ese contador. La víctima es el
  marco con el valor más pequeño. Con 64 marcos, recorrerlos todos en cada desalojo es
  irrelevante en costo; no hace falta lista enlazada.
- **FIFO**: un arreglo circular de tamaño `num_marcos` con un índice de cabeza, o
  simplemente un `orden_llegada[]` con la misma idea del contador.

En el reporte hay que decir explícitamente que en un SO real LRU exacto es demasiado
costoso (habría que actualizar estructuras en cada acceso a memoria) y por eso se
aproxima con el **algoritmo del reloj** y el bit de referencia (OSTEP cap. 22.8).

### Selección por línea de comandos

```
./simulador <archivo_entrada> [-p lru|fifo] [-m <KB_memoria_fisica>]
```

Por defecto: `-p lru` y memoria física de 256 KB. La salida final debe imprimir cuál
política se usó.

### Qué esperar en los resultados (para no asustarse)

Según OSTEP cap. 22: sin localidad, FIFO y LRU rinden casi igual; con localidad tipo
80-20, LRU gana; en un **recorrido cíclico** más grande que la memoria (el caso de
`tests/t3_estres.txt`), **las dos se hunden casi a 0% de aciertos**. Ese resultado malo
es el esperado, no un bug: es el peor caso de ambas políticas.

## Fases de trabajo

1. **Esqueleto**: `Makefile`, estructura de archivos, `main` que lee el archivo de
   entrada y parsea los comandos. Sin traducción todavía.
2. **Estructuras**: directorio, tablas de nivel 2, memoria física (arreglo de marcos),
   lista de marcos libres, PTE con sus bits.
3. **Traducción VA→PA** de una página ya presente, con pruebas manuales de que los
   campos PT1 / PT2 / offset se extraen bien.
4. **alloc / free**: crear tablas de nivel 2 bajo demanda y marcar páginas como válidas.
5. **Fallos de página**: asignar marco libre, contar fallos.
6. **FIFO** detrás de la interfaz de `replace.h`, cuando no hay marcos libres,
   contando reemplazos. Se hace primero por ser la más simple: así, si los números
   salen raros, se sabe que el error está en el simulador y no en la política.
7. **LRU** detrás de la misma interfaz, y el flag `-p` para escoger. Verificar que
   con la misma prueba LRU da igual o mejor hit rate que FIFO (salvo en el caso
   cíclico, donde las dos se hunden).
8. **Estadísticas y salida** con el formato exacto pedido.
9. **Pruebas**: los tres archivos de `tests/`, corridos **con las dos políticas**,
   guardando los resultados en una tabla para el reporte.
10. **Calidad**: valgrind, `-Wall -Werror`, comentarios, README.
11. **Reporte de análisis**, con la comparación LRU vs FIFO basada en los números
    medidos, no solo en la teoría.

## Archivos sugeridos

```
src/main.c        parseo de comandos y ciclo principal
src/mmu.c/.h      traducción VA→PA, extracción de campos
src/pagetable.c/.h  directorio, tablas nivel 2, PTEs, alloc/free
src/physmem.c/.h  marcos, lista de libres, fallos de página
src/replace.c/.h  interfaz de política + implementaciones LRU y FIFO
src/stats.c/.h    contadores y reporte final
tests/*.txt       programas de prueba
Makefile
README.md
REPORTE.md
```

## Cómo verificar cada cosa

- **Traducción**: con página de 4 KB, la VA `0x00001004` tiene PT1 = 0, PT2 = 1 y
  offset = 4. Si PT2 no da 1, las máscaras están mal.
- **Hit rate**: los primeros accesos a páginas nuevas **siempre** son fallos
  (compulsory misses). Si el primer acceso da hit, hay un error.
- **Reemplazos**: solo puede haber reemplazos después de que se hayan usado todos los
  marcos. Con 64 marcos, el reemplazo número 1 no puede ocurrir antes del fallo 65.
- **Suma de control**: accesos = hits + fallos.
