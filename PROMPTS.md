# Cómo trabajar este laboratorio con Claude Code

## Preparación (una sola vez)

1. Crea el repo y entra a la carpeta:

```bash
mkdir lab-memoria-virtual && cd lab-memoria-virtual
git init
```

2. Copia dentro de la carpeta: `CLAUDE.md`, `docs/lab-spec.md` y `tests/`.

3. La política ya está decidida en `CLAUDE.md`: **LRU como principal y FIFO como
   política adicional** (apunta al bonus de la rúbrica y permite comparar con datos
   medidos). No hay nada que completar.

4. Arranca Claude Code dentro de esa carpeta:

```bash
claude
```

Claude Code lee `CLAUDE.md` automáticamente al iniciar, así que ya no tienes que
volver a explicarle el laboratorio en cada sesión.

## Mensaje inicial

Pégale esto tal cual en la primera sesión:

> Lee `CLAUDE.md` y `docs/lab-spec.md`. Vamos a hacer la **fase 1** (esqueleto):
> Makefile con los targets `all`, `clean` y `run`, la estructura de carpetas que
> propone CLAUDE.md, y un `main.c` que reciba la ruta de un archivo de entrada por
> argumento, lo lea línea por línea y parsee los comandos `alloc`, `write`, `read` y
> `free`, imprimiendo por ahora lo que parseó. Nada de traducción ni de tablas
> todavía. Antes de escribir código, muéstrame el plan y las firmas de las funciones
> que vas a crear, y dime qué decisiones estás tomando por mí.

Cuando compile y corra con `./simulador tests/t1_basico.txt`, sigues con la fase 2.

## Prompts por fase

Uno por sesión o por bloque de trabajo. No los pegues todos de una.

**Fase 2 · Estructuras**

> Fase 2: define las estructuras de datos. Necesito el directorio de nivel 1 con 1024
> entradas, las tablas de nivel 2 creadas bajo demanda, el arreglo de marcos físicos
> (configurable, por defecto 256 KB) y la lista de marcos libres. La PTE debe tener
> PFN y los bits valid, present, accessed y dirty. Explícame por qué elegiste cada
> estructura y cuánta memoria ocupa cada una.

**Fase 3 · Traducción**

> Fase 3: implementa la traducción VA→PA en `mmu.c`, suponiendo que la página ya está
> presente. Quiero funciones separadas para extraer PT1, PT2 y offset. Después
> escribe un pequeño programa de prueba o unos `printf` temporales que verifiquen con
> estos casos: VA 0x00001004 → PT1=0, PT2=1, offset=4; VA 0x00400000 → PT1=1, PT2=0,
> offset=0. Muéstrame la salida antes de seguir.

**Fase 4 · alloc y free**

> Fase 4: implementa `alloc <bytes>` y `free <virtual_addr>`. `alloc` debe marcar
> como válidas las páginas necesarias y crear la tabla de nivel 2 solo si no existe.
> Explícame qué decisión tomaste sobre desde qué dirección virtual empieza a asignar
> y por qué, y qué pasa si se hace `free` de una dirección que no fue asignada.

**Fase 5 · Fallos de página**

> Fase 5: implementa el manejo de fallos de página. Si hay marcos libres, asigna uno y
> marca present=1. Lleva el contador de fallos. Todavía sin política de reemplazo: si
> no hay marcos libres, por ahora imprime un error claro. Recuérdame la diferencia
> entre valid=0 y present=0 y verifica que el código la respete.

**Fase 6 · FIFO**

> Fase 6: implementa la interfaz de política de `CLAUDE.md` en `replace.h` y la
> primera implementación, **FIFO**. Nada del resto del código puede preguntar qué
> política está activa. Importante: cuando se reemplaza una página hay que actualizar
> la PTE de la víctima (present=0) y contar el reemplazo. Explícame la estructura que
> usaste y su costo.

**Fase 7 · LRU y el flag -p**

> Fase 7: agrega **LRU** detrás de la misma interfaz, con un contador global de accesos
> y un arreglo de último uso por marco. Recuerda que en LRU hay que actualizar el uso
> también en los aciertos, no solo en los fallos. Agrega el flag `-p lru|fifo` (por
> defecto lru) y el flag `-m` para el tamaño de memoria física. Luego corre
> `tests/t2_localidad.txt` con las dos políticas y compáralas.

**Fase 8 · Estadísticas**

> Fase 8: implementa la salida final con el formato exacto del enunciado: total de
> accesos, total de fallos, hit rate, total de reemplazos y política. Verifica la suma
> de control: accesos = hits + fallos.

**Fase 9 · Pruebas**

> Fase 9: corre los tres archivos de `tests/` **con las dos políticas** (seis
> corridas) y arma una tabla con accesos, fallos, hit rate y reemplazos. Para
> `t3_estres.txt` explícame si el número de reemplazos tiene sentido con 64 marcos y
> 100 páginas distintas. Si algo no cuadra, dime dónde puede estar el error antes de
> cambiar código.

**Fase 10 · Calidad**

> Fase 10: compila con `gcc -Wall -Werror -std=c99` y arregla todo lo que salga. Luego
> corre `valgrind --leak-check=full ./simulador tests/t3_estres.txt` y arregla las
> fugas. Muéstrame la salida de valgrind antes y después.

**Fase 11 · Entregables**

> Fase 11: escribe el `README.md` con instrucciones de compilación y uso, y un borrador
> de `REPORTE.md` con: descripción de las estructuras de datos, explicación de la
> política implementada, resultados de las tres pruebas, análisis del hit rate y los
> reemplazos, y la comparación LRU vs FIFO **con la tabla de resultados medidos** de la fase 9,
> además de la comparación teórica. El análisis lo reviso y lo completo yo, no
> inventes conclusiones que los datos no muestren.

## Consejos de uso

- **Plan mode** (Shift+Tab) para que proponga el plan sin tocar archivos. Útil en las
  fases 2 y 6.
- Si una respuesta se desvía, corta con Esc y reformula. Sale más barato que dejarlo
  escribir 300 líneas equivocadas.
- Haz **commit al terminar cada fase**. Así puedes devolverte si algo se daña.
- Cuando algo no compile, pega el error completo; no lo resumas.
- Pídele que te explique cualquier línea que no entiendas. Te van a preguntar en la
  sustentación, y el reporte vale 15%.
- Antes de entregar, pídele: *"revisa el proyecto completo contra `docs/lab-spec.md` y
  dime qué falta de la rúbrica"*.

## Reparto del trabajo en grupo (son hasta 3)

- Persona A: estructuras + traducción (fases 2 y 3)
- Persona B: alloc/free + fallos de página (fases 4 y 5)
- Persona C: políticas de reemplazo FIFO y LRU + estadísticas (fases 6, 7 y 8)
- Las fases 9, 10 y 11 las hacen juntos.

Trabajen en ramas separadas y revisen los merges, porque `pagetable.h` lo tocan todos.
