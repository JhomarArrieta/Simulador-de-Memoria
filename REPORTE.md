# Reporte de análisis — Simulador de memoria virtual con paginación de dos niveles

**Curso:** Sistemas Operativos, Universidad de Antioquia, 2026-2
**Integrantes:** [COMPLETAR: nombres y documentos]
**Política principal implementada:** LRU. **Política adicional:** FIFO.

> **Estado de este documento:** borrador. Todas las cifras están medidas con el
> código de este repositorio y son reproducibles con los comandos indicados. Las
> secciones marcadas con **[COMPLETAR]** requieren juicio propio del grupo y están
> deliberadamente vacías.

---

## 1. El modelo simulado

El simulador **no** reserva memoria del sistema operativo para el proceso simulado:
modela con estructuras de datos en C un espacio virtual, una memoria física y las
tablas de páginas que las relacionan.

| Parámetro | Valor |
|---|---|
| Tamaño de página | 4 KB (`TAM_PAGINA`, configurable en `src/config.h`) |
| Espacio virtual | 32 bits = 4 GB, 2²⁰ = 1 048 576 páginas posibles |
| Memoria física | configurable con `-m`, por defecto 256 KB = **64 marcos** |
| Niveles de tabla | 2 (directorio de 1024 entradas + tablas de 1024 entradas) |

Dirección virtual:

```
 31            22 21            12 11                 0
+----------------+----------------+-------------------+
|   PT1 (10 b)   |   PT2 (10 b)   |   offset (12 b)   |
+----------------+----------------+-------------------+
```

La extracción usa máscaras derivadas del formato, no constantes escritas a mano
(`src/mmu.c`):

```c
#define MASCARA_NIVEL1 ((1u << BITS_NIVEL1) - 1u)   /* 0x3FF */

uint32_t mmu_pt1(uint32_t va) { return (va >> (BITS_NIVEL2 + BITS_OFFSET)) & MASCARA_NIVEL1; }
uint32_t mmu_pt2(uint32_t va) { return (va >> BITS_OFFSET) & MASCARA_NIVEL2; }
uint32_t mmu_offset(uint32_t va) { return va & MASCARA_OFFSET; }
```

Si mañana la página fuera de 8 KB, basta cambiar `BITS_OFFSET` en `config.h`.

La dirección física se arma dejando el offset intacto:

```c
pa = (pfn << BITS_OFFSET) | offset;   /* equivale a pfn * TAM_PAGINA + offset */
```

### Verificación de la traducción

| VA | PT1 | PT2 | offset | Comentario |
|---|---|---|---|---|
| `0x00001004` | 0 | 1 | 4 | caso del enunciado |
| `0x00400000` | 1 | 0 | 0 | primera VA de la segunda región de 4 MB |
| `0x00000FFF` | 0 | 0 | 4095 | último byte de la primera página |
| `0x003FFFFF` | 0 | 1023 | 4095 | último byte de la primera región de 4 MB |
| `0xFFFFFFFF` | 1023 | 1023 | 4095 | última dirección del espacio |

Con `pfn = 3`, la VA `0x00001004` produce la PA `0x00003004`: cambió la parte alta
(página 1 → marco 3) y el offset `0x004` sobrevivió intacto. Verificable con
`./simulador <archivo> -v`, que imprime la PA de cada acceso.

---

## 2. Descripción de las estructuras de datos

*(Criterio de la rúbrica: "Descripción de estructuras de datos")*

### 2.1 La PTE: `pte_t` — 4 bytes

```c
typedef struct {
    unsigned int pfn      : 20; /* marco fisico; 20 bits cubren las 2^20 paginas */
    unsigned int valid    : 1;  /* la pagina fue asignada por el proceso */
    unsigned int present  : 1;  /* la pagina esta en memoria fisica */
    unsigned int accessed : 1;  /* referenciada por un read o un write */
    unsigned int dirty    : 1;  /* modificada por un write */
} pte_t;
```

Se usan **campos de bits** y no cuatro `uint8_t` por dos razones: es como se ve una
PTE real de x86 de 32 bits, donde los bits de control viven en la misma palabra que
el número de marco (OSTEP cap. 18), y reduce la entrada de 8 a **4 bytes** — la
mitad de memoria en una estructura que se replica 1024 veces por tabla. El PFN usa
20 bits porque el espacio virtual tiene 2²⁰ páginas: aunque el simulador nunca
tenga más de 64 marcos, el campo puede direccionar cualquier marco posible del
modelo.

`src/pagetable.c` incluye una verificación en tiempo de compilación: si la PTE deja
de medir 4 bytes, el proyecto no compila.

```c
typedef char verificacion_tam_pte[(sizeof(pte_t) == 4) ? 1 : -1];
```

### 2.2 Tabla de nivel 2: `tabla_nivel2_t` — 4 104 bytes

1024 PTEs (4 096 B) más un contador `paginas_validas`. El contador permite detectar
que la tabla quedó sin páginas válidas para liberarla con `free()`.

### 2.3 Directorio: `directorio_t` — 8 232 bytes

1024 punteros a tablas de nivel 2 (8 192 B en x86-64) más el contador de tablas
vivas, el puntero del espacio virtual y el registro de asignaciones.

Es un **arreglo fijo de punteros, no una tabla hash**: el índice PT1 *es* la
posición, así que la consulta es un acceso directo sin recorrer ni comparar nada, y
`NULL` codifica gratis "esta región de 4 MB no tiene ninguna página válida". Ese
`NULL` es toda la ventaja de la tabla multinivel.

### 2.4 Marcos: `marco_t` — 12 bytes, y la lista de libres

```c
typedef struct {
    int      ocupado;
    uint32_t vpn;       /* pagina virtual alojada (pt1 << 10 | pt2) */
    int      siguiente; /* siguiente marco de la lista de libres, o -1 */
} marco_t;
```

El campo `vpn` es el **mapeo inverso**: al desalojar una víctima hay que poner
`present = 0` en *su* PTE, y para eso el marco necesita saber de qué página es. Se
guarda el número y no un `pte_t *` para no quedar con un puntero colgante si `free`
libera la tabla de nivel 2 mientras el marco sigue ocupado.

La **lista de marcos libres es intrusiva**: un índice de cabeza más el campo
`siguiente` de cada marco, con `-1` como fin. Cuesta 4 bytes por marco, es O(1) al
tomar y al devolver, y no necesita un `malloc` por nodo, lo que reduce la
superficie para fugas. Se inicializa 0 → 1 → 2 → …, así que los primeros fallos
ocupan marcos en orden ascendente y la traza se lee con facilidad.

El contenido de la memoria física es un bloque contiguo de `num_marcos × 4096`
bytes: **un byte por dirección**, lo que mantiene la correspondencia VA ↔ byte
exacta y evita que un valor de varios bytes se parta entre dos páginas. Al entregar
un marco se pone en ceros, igual que un SO real hace *demand zeroing* para no
mostrarle a un proceso los datos del anterior.

### 2.5 Registro de asignaciones: `asignacion_t` — 8 bytes

`free <dir>` tiene que liberar la asignación **completa**, igual que `free(ptr)` en
C, así que el directorio guarda un arreglo dinámico de `{vpn_inicio, paginas}` que
crece duplicando su capacidad (costo amortizado O(1)).

### 2.6 Resumen de tamaños (medidos, no calculados a mano)

Los imprime el propio programa con `-v`, tomados de `sizeof`:

| Estructura | Tamaño | Cuántas hay |
|---|---|---|
| `pte_t` | **4 B** | 1024 por tabla de nivel 2 |
| `tabla_nivel2_t` | **4 104 B** | solo las regiones de 4 MB con páginas válidas |
| `directorio_t` | **8 232 B** | una, desde el arranque |
| `marco_t` | **12 B** | una por marco (768 B con 64 marcos) |
| `asignacion_t` | **8 B** | una por `alloc` vivo |
| memoria física completa | **262 944 B** | 256 KB de datos + metadatos |

### 2.7 El ahorro de la tabla de dos niveles

Este es el número central del capítulo 20 de OSTEP, medido en este simulador:

| Escenario | Memoria de traducción |
|---|---|
| Tabla de **un solo nivel** para 2²⁰ páginas | **4 194 304 B, siempre** |
| Dos niveles, `t1_basico` (toca 1 región de 4 MB) | **12 400 B** |
| Dos niveles, `alloc` del espacio completo de 4 GB | **4 210 792 B** |

La primera fila se paga completa aunque el proceso use una sola página. La segunda
es un factor **~338 menor**. La tercera es el peor caso y merece decirse con
claridad: cuando el proceso toca **todo** el espacio virtual, la tabla de dos
niveles cuesta lo mismo que la de un nivel más el directorio. La multinivel no
ahorra memoria; ahorra memoria **en espacios virtuales dispersos**, que es el caso
real de cualquier proceso.

Reproducible con `./simulador <archivo> -v` (línea `memoria de traduccion`).

---

## 3. Traducción, fallos de página y la diferencia `valid` / `present`

*(Criterio de la rúbrica: "Funcionalidad core")*

`mmu_traducir` recorre los dos niveles y devuelve uno de tres resultados. La
distinción entre los dos primeros es el punto conceptual del laboratorio:

| Estado de la PTE | Resultado | Qué hace el simulador | Contadores |
|---|---|---|---|
| Sin tabla de nivel 2, o `valid = 0` | `TRAD_SEGFAULT` | **Acceso ilegal**. No trae nada a memoria | `ilegales++`; **no** cuenta como acceso |
| `valid = 1, present = 0` | `TRAD_FALLO_PAGINA` | **Fallo de página**: consigue marco, `present = 1`, completa el acceso | `accesos++` y `fallos++` |
| `valid = 1, present = 1` | `TRAD_OK` | Acierto: arma la PA y toca el marco | `accesos++` |

`valid = 0` significa que la página **nunca se asignó**: es el segmentation fault de
un programa real. `valid = 1, present = 0` significa que la página **existe** pero
está fuera de memoria: eso sí es un fallo de página. Confundirlos contaminaría el
hit rate con accesos que nunca ocurrieron; por eso los ilegales van en un contador
aparte y se mantiene `accesos = hits + fallos`.

`mmu_traducir` **no modifica** `accessed` ni `dirty`. En hardware real la MMU los
pone durante la traducción, pero dejarla sin efectos secundarios la hace verificable
en una prueba unitaria y respeta la separación de responsabilidades: quien ejecuta
el acceso (`src/fallos.c`) recibe el puntero a la PTE y marca los bits ahí, sin
volver a recorrer la tabla.

**Verificación de que el código respeta la diferencia.** La *misma* dirección virtual
da resultados distintos según el estado de su PTE:

```
read 0                 -> acceso ilegal      (antes de cualquier alloc: valid=0)
alloc 8192
write 0 42             -> FALLO marco 0      (valid=1, present=0)
read 0                 -> hit   marco 0      (valid=1, present=1)
free 0
read 0                 -> acceso ilegal      (free devolvio la pagina a valid=0)
```

**`alloc` no reserva memoria física**: solo marca `valid = 1, present = 0` y crea la
tabla de nivel 2 si no existe. Los marcos se asignan en el primer acceso —
*demand paging* —, y por eso el primer acceso a cada página es siempre un fallo.

**Al desalojar una víctima**, `present` pasa a 0 pero **`valid` se queda en 1**: la
página no deja de existir, solo deja de estar en memoria. Si el proceso la vuelve a
tocar es otro fallo de página, no un acceso ilegal.

---

## 4. La política de reemplazo

*(Criterio de la rúbrica: "Política de reemplazo" — 25 %)*

### 4.1 La interfaz

La política vive detrás de la interfaz de `src/replace.h`, y **ningún otro archivo
del proyecto pregunta qué política está activa**:

```c
typedef enum { POLITICA_LRU, POLITICA_FIFO } politica_t;

int         politica_init(politica_t p, int num_marcos);
void        politica_al_cargar(int marco);   /* una pagina entro a este marco */
void        politica_al_acceder(int marco);  /* en CADA acceso, hit o fallo   */
int         politica_elegir_victima(void);
void        politica_liberar(void);
const char *politica_nombre(void);
```

La única mención de un `POLITICA_*` fuera de `replace.c` es la inicialización en
`main.c`. Para reportar la política al final, `main` llama a `politica_nombre()` en
vez de decidir el texto con un `if`.

El estado vive como `static` dentro de `replace.c`, porque la interfaz no lleva
parámetro de contexto. Es estado privado del módulo (*internal linkage*), invisible
desde cualquier otro archivo, creado por `politica_init` y liberado por
`politica_liberar`: no es una variable global del programa. La única limitación que
impone es que no puede haber dos políticas vivas a la vez, y el simulador nunca lo
necesita.

### 4.2 La estructura: una sola para las dos políticas

```c
static unsigned long *marca;    /* marca[m]: cuando el marco m fue cargado/usado */
static unsigned long  contador; /* crece con cada evento */
```

La víctima es siempre el marco con la **marca más pequeña**. Lo único que cambia es
*cuándo* se actualiza la marca:

```c
void politica_al_cargar(int marco)  { marca[marco] = ++contador; }  /* las dos */

void politica_al_acceder(int marco)
{
    if (politica_activa == POLITICA_LRU) {
        marca[marco] = ++contador;   /* FIFO no hace nada aqui */
    }
}
```

- **FIFO**: la marca se pone solo al cargar → es el **orden de llegada**. Un acierto
  no reordena nada: la página sale por antigüedad, sin importar cuánto se use.
- **LRU**: la marca se pone al cargar **y en cada acceso** → es el **último uso**.

`politica_al_acceder` se llama desde `fallos.c` al final de *todo* acceso
completado, hit o fallo, y no dentro del manejo del fallo. Si estuviera ahí, LRU
nunca vería los aciertos y una página muy usada se vería igual de vieja que una que
solo se tocó al cargarla: sería FIFO con otro nombre. Es el error clásico al
implementar LRU.

El pre-incremento (`++contador`) deja el 0 reservado para "este marco nunca recibió
una página", lo que permite el `assert(marca[victima] != 0)` que atrapa el error de
pedir un desalojo cuando todavía había marcos libres.

### 4.3 Costo

| Operación | Costo | Frecuencia |
|---|---|---|
| `politica_al_cargar` | O(1) | una vez por fallo de página |
| `politica_al_acceder` | O(1) (FIFO: nada) | en **cada** acceso |
| `politica_elegir_victima` | **O(num_marcos)** | solo en un desalojo |
| Memoria | 8 B por marco (512 B con 64 marcos) | |

El recorrido O(64) del desalojo es irrelevante: ocurre una vez por reemplazo, no por
acceso, y son 64 comparaciones de enteros. Una lista enlazada lo bajaría a O(1) a
cambio de punteros y del problema que se describe abajo.

### 4.4 Por qué marcas y no una cola enlazada

La interfaz obligatoria **no tiene** un `politica_al_liberar`, así que cuando `free`
devuelve un marco a la lista de libres, la política no se entera. Con marcas eso se
corrige solo: al reusar el marco, `politica_al_cargar` sobrescribe su marca y el
orden queda correcto. Una cola enlazada dejaría ese marco **duplicado** dentro de la
cola y acabaría desalojando un marco que ya no le pertenece a esa página. Es un bug
que solo aparece al mezclar `free` con memoria llena.

### 4.5 LRU exacto no es implementable en un SO real

Lo que hace este simulador es LRU **exacto**: actualiza una marca en cada acceso a
memoria. Un sistema operativo real no puede hacerlo, porque implicaría que el
hardware escribiera en una estructura del kernel en *cada* referencia a memoria — el
costo sería prohibitivo. Por eso los SO reales **aproximan** LRU con el bit de
referencia (`accessed`) y el **algoritmo del reloj**: recorren los marcos en círculo,
y si el bit está en 1 lo ponen en 0 y siguen; la víctima es el primero que encuentren
con el bit ya en 0 (OSTEP cap. 22.8).

Este simulador ya mantiene el bit `accessed` en cada acceso y lo limpia al desalojar,
así que la información que el algoritmo del reloj necesita está disponible: es la
extensión natural del proyecto.

---

## 5. Resultados de las pruebas

*(Criterio de la rúbrica: "Resultados de 2-3 programas de prueba")*

Configuración: página de 4 KB, memoria física de 256 KB = **64 marcos**.
Reproducible con `./simulador tests/<archivo> -p <lru|fifo>`.

### 5.1 Las seis corridas

| Test | Política | Accesos | Fallos | Hit rate | Reemplazos | Páginas distintas | Fallos mínimos posibles |
|---|---|---|---|---|---|---|---|
| `t1_basico.txt` | LRU | 4 | 2 | 50,00 % | 0 | 2 | 2 |
| `t1_basico.txt` | FIFO | 4 | 2 | 50,00 % | 0 | 2 | 2 |
| `t2_localidad.txt` | LRU | 60 | 4 | 93,33 % | 0 | 4 | 4 |
| `t2_localidad.txt` | FIFO | 60 | 4 | 93,33 % | 0 | 4 | 4 |
| `t3_estres.txt` | LRU | 400 | 200 | 50,00 % | 136 | 100 | 100 |
| `t3_estres.txt` | FIFO | 400 | 200 | 50,00 % | 136 | 100 | 100 |

La última columna son los *compulsory misses*: la primera vez que se toca una
página, ninguna política puede evitar el fallo. Es la referencia contra la cual se
mide si una política hizo un buen trabajo.

### 5.2 Qué hace cada prueba

- **`t1_basico.txt`** — el ejemplo del enunciado: `alloc 8192` y cuatro accesos a dos
  páginas. Sirve para verificar la traducción y los *compulsory misses*.
- **`t2_localidad.txt`** — 60 accesos sobre **4 páginas**, con localidad temporal y
  espacial (se vuelve a la misma página y a offsets vecinos).
- **`t3_estres.txt`** — `alloc` de 100 páginas y **dos pasadas** de barrido cíclico,
  cada página con un `write` seguido de un `read`. 100 páginas distintas contra 64
  marcos: el conjunto de trabajo no cabe.

### 5.3 Barrido del tamaño de memoria sobre `t3_estres.txt`

Las 100 páginas distintas ocupan exactamente 400 KB:

| Memoria | Marcos | Fallos | Hit rate | Reemplazos | `fallos − marcos` |
|---|---|---|---|---|---|
| 256 KB | 64 | 200 | 50,00 % | 136 | 136 ✓ |
| 384 KB | 96 | 200 | 50,00 % | 104 | 104 ✓ |
| 396 KB | 99 | 200 | 50,00 % | 101 | 101 ✓ |
| 400 KB | 100 | **100** | **75,00 %** | **0** | — |
| 512 KB | 128 | 100 | 75,00 % | 0 | — |

Idéntico con LRU y con FIFO en las cinco configuraciones.

---

## 6. Análisis del hit rate y de los reemplazos

*(Criterio de la rúbrica: "Análisis: cambio de hit rate, número de reemplazos")*

### 6.1 `t1` y `t2` alcanzan el mínimo teórico

En `t1` (2 fallos para 2 páginas) y en `t2` (4 fallos para 4 páginas) **todos** los
fallos son obligatorios. Ninguna política —ni siquiera la óptima de Belady, que
conoce el futuro— podría bajar de ahí. El 93,33 % de `t2` es consecuencia directa de
la localidad: 4 páginas se reutilizan 60 veces, y a partir del quinto acceso todo
es acierto.

En los dos casos hay **0 reemplazos**, y la razón es estructural: 2 y 4 páginas
caben de sobra en 64 marcos, así que `politica_elegir_victima` no se llama ni una
vez. **La política de reemplazo es irrelevante mientras el conjunto de trabajo quepa
en memoria.** Es el resultado más útil de estas dos pruebas.

### 6.2 Por qué `t3` produce exactamente 136 reemplazos

`t3` toca 100 páginas distintas en 64 marcos, dos veces.

**Primera pasada — 100 fallos.** Cada página se toca por primera vez: el `write`
falla y el `read` inmediato acierta. Los primeros **64** fallos se atienden con
marcos libres, sin desalojar a nadie; los **36** restantes (páginas 64–99) ya
necesitan víctima → **36 reemplazos**.

**Segunda pasada — otros 100 fallos.** Al terminar la primera pasada, en memoria
están las páginas **36–99** (las últimas 64 cargadas). Llega la página 0: no está →
fallo, y se desaloja la página 36. Llega la página 1 → se desaloja la 37. Cuando el
barrido llega a la página 36, hace 36 accesos que fue expulsada. **Todas las páginas
fallan**, y cada fallo cuesta un desalojo → **100 reemplazos**.

```
36 + 100 = 136 reemplazos
```

Medido: los fallos por pasada son 100 y 100. La segunda pasada no aprovecha **nada**
de la primera.

### 6.3 Dos sumas de control

1. **`accesos = hits + fallos`** — se cumple en las 6 corridas, y en 28
   combinaciones de archivo, política y tamaño de memoria. El simulador la imprime
   con `-v`. Los hits no se guardan en un contador: se derivan de
   `accesos − fallos`, para que no puedan desincronizarse.
2. **`reemplazos = fallos − marcos`** — se cumple siempre que la memoria se llene
   una vez y no se libere: 136 = 200 − 64, 104 = 200 − 96, 101 = 200 − 99. Sale de
   que los primeros `marcos` fallos se atienden con marcos libres y cada fallo
   posterior cuesta exactamente un desalojo.

Además, el **primer reemplazo ocurre en el fallo 65**, no antes: el fallo 64 recibe
el marco 63 (el último libre) y el fallo 65 reutiliza el marco 0.

### 6.4 El 50 % de `t3` es engañoso

Un 50 % parece un rendimiento mediocre pero aceptable. No lo es: **es el peor caso
disfrazado**. El archivo toca cada página **dos veces seguidas** (`write` y luego
`read`), así que el `read` siempre acierta sobre la página que el `write` acaba de
traer. Si `t3` tocara cada página una sola vez por pasada, el hit rate sería **0 %**.
Lo que el 50 % muestra realmente es que **el 100 % de los primeros accesos a cada
página fallan en las dos pasadas**.

### 6.5 El acantilado: un marco de diferencia

El barrido de memoria de §5.3 es el resultado más contundente. Con **99 marcos para
100 páginas** —un solo marco de menos— el rendimiento es **idéntico** al de 64
marcos: 200 fallos, 50 %. Con 100 marcos cae de golpe a los 100 fallos obligatorios
y sube a 75 %.

No hay mejora gradual: es un **acantilado**. La razón es la interacción entre el
patrón cíclico y ambas políticas: mientras falte aunque sea un marco, la página que
se desaloja es siempre la que se va a necesitar enseguida, y el efecto se propaga en
cadena por todo el barrido.

### 6.6 El costo oculto: las páginas sucias

De los 136 desalojos de `t3`, **los 136** eran de páginas con `dirty = 1`, porque
cada página se escribe antes de leerse. En un SO real cada uno de esos desalojos
exigiría **escribir la página al disco** antes de soltar el marco. El hit rate de
50 % no captura ese costo: dos políticas con el mismo hit rate pueden tener costos
de E/S muy distintos según cuántas víctimas estén sucias. El simulador lleva ese
contador (visible con `-v`) precisamente para poder decirlo con un número.

**[COMPLETAR: si el grupo quiere, comparar los desalojos sucios entre LRU y FIFO en
un caso donde las políticas difieran, y discutir si el hit rate es la métrica
adecuada.]**

---

## 7. Comparación LRU vs FIFO

*(Criterio de la rúbrica: "Comparación teórica con otra política")*

### 7.1 Lo que muestran los datos medidos

**Las tres pruebas del enunciado dan resultados idénticos con las dos políticas.**
Eso no es un error de implementación, y las razones son distintas en cada caso:

| Test | Resultado | Por qué |
|---|---|---|
| `t1_basico` | empate (2 fallos) | 2 páginas en 64 marcos: **cero reemplazos**, la política nunca se consulta |
| `t2_localidad` | empate (4 fallos) | 4 páginas en 64 marcos: **cero reemplazos**, ídem |
| `t3_estres` | empate (200 fallos, 136 reemplazos) | barrido cíclico: es el **peor caso de las dos** |

El empate de `t3` es el resultado teóricamente esperado, no una coincidencia: en un
recorrido secuencial, "la página que llegó primero" (FIFO) y "la que se usó hace más
tiempo" (LRU) **son la misma página**. Las dos desalojan la página 36 cuando entra la
página 0, que es justo la que se necesitará enseguida. Cada desalojo es el peor
posible, para ambas.

### 7.2 Dónde sí se separan

Para observar una diferencia hace falta un caso con **presión de memoria** y una
página **caliente pero antigua**. Prueba construida para eso: 64 páginas llenan la
memoria, la página 0 se lee 5 veces más, y luego llega una página nueva.

| Política | Accesos | Fallos | Hit rate | Reemplazos |
|---|---|---|---|---|
| LRU | 71 | **65** | **8,45 %** | **1** |
| FIFO | 71 | **66** | 7,04 % | **2** |

La línea que las separa:

```
LRU   write 0x00040000 -> FALLO marco  1     <- desaloja la pagina 1
      read  0x00000000 -> hit   marco  0     <- la pagina 0 sobrevivio

FIFO  write 0x00040000 -> FALLO marco  0     <- desaloja la pagina 0
      read  0x00000000 -> FALLO marco  1     <- y hay que traerla de vuelta
```

La página 0 era la **más antigua** pero se acababa de usar 5 veces. LRU la protegió;
FIFO la botó y tuvo que recargarla: un fallo y un reemplazo más.

El archivo se genera con:

```bash
{ echo "alloc 266240"
  for i in $(seq 0 63); do echo "write $((i*4096)) $i"; done
  for i in 1 2 3 4 5; do echo "read 0"; done
  echo "write 262144 64"
  echo "read 0"; } > /tmp/discriminante.txt
./simulador /tmp/discriminante.txt -p lru
./simulador /tmp/discriminante.txt -p fifo
```

> **Advertencia honesta sobre el alcance de este dato.** Es **una** prueba sintética
> y la diferencia es de **un solo fallo** (65 contra 66). Demuestra que las dos
> políticas están correctamente implementadas y que se comportan como la teoría
> predice, pero **no** sustenta una afirmación cuantitativa del tipo "LRU es X %
> mejor que FIFO". Para eso haría falta una prueba con localidad tipo 80-20 y
> presión de memoria sostenida, que no está en `tests/`.
>
> **[DECIDIR: agregar `tests/t4_presion.txt` con localidad 80-20 sobre ~80 páginas
> para obtener una diferencia medible, o dejar la comparación apoyada en la teoría
> más este caso.]**

En **ninguna** de las corridas realizadas LRU salió peor que FIFO, que es la
propiedad que había que verificar.

### 7.3 La comparación teórica

Según OSTEP cap. 22, el comportamiento relativo de las políticas depende por
completo de la carga de trabajo:

- **Sin localidad** (accesos aleatorios uniformes): FIFO, LRU y Random rinden
  prácticamente igual. Si no hay patrón que explotar, no hay información que una
  política pueda aprovechar mejor que otra.
- **Con localidad tipo 80-20** (el 80 % de los accesos van al 20 % de las páginas):
  **LRU gana**, porque conserva justamente ese 20 % caliente. FIFO lo desaloja por
  antigüedad, sin importar que se esté usando.
- **En un recorrido cíclico más grande que la memoria**: **las dos se hunden**, y
  Random puede incluso superarlas, porque su aleatoriedad rompe la cadena de
  desalojos sistemáticamente equivocados. `t3_estres.txt` es exactamente este caso,
  y el empate medido en 200 fallos lo confirma.

**Costo de implementación**, que es la otra mitad del trade-off:

| | FIFO | LRU |
|---|---|---|
| Trabajo en un acierto | **ninguno** | actualizar el último uso |
| Estructura mínima | cola o contador de llegada | marca de último uso por marco |
| En este simulador | 8 B/marco, O(1) / O(n) | igual |
| **En un SO real** | trivial y barato | **impagable en su forma exacta** |

FIFO es determinista, trivial de implementar y no cuesta nada en el camino rápido
(el acierto). LRU necesita tocar una estructura en cada referencia a memoria, lo que
en hardware real es inviable; de ahí el algoritmo del reloj descrito en §4.5. La
conclusión práctica es que LRU no se elige sobre FIFO por ser "mejor" en abstracto,
sino porque **aproximaciones baratas de LRU** capturan casi toda su ventaja a un
costo que FIFO también paga.

**[COMPLETAR: conclusión del grupo. Con los datos medidos, ¿qué política elegirían
para este simulador y por qué? ¿Cambiaría la respuesta si la carga fuera la de
`t2_localidad` en vez de la de `t3_estres`?]**

---

## 8. Limitaciones del modelo

Decisiones de modelado, no defectos; se declaran para que los resultados se lean
correctamente.

1. **No hay área de intercambio (swap).** Una página desalojada pierde su contenido:
   al recargarla, el marco llega en ceros. Medido: se escribe 7 en la página 0, se
   desaloja, y al volver a leerla devuelve 0. Un SO real habría escrito la página al
   disco (los 136 desalojos sucios de `t3`) y la habría recuperado de allí.
2. **Un byte por dirección.** Un valor mayor que 255 se trunca avisando por `stderr`.
3. **El espacio virtual no se reutiliza.** `alloc` reparte desde la dirección 0 con
   un puntero que solo sube, como `brk`/`sbrk`. Un `malloc` real reutilizaría el
   espacio liberado.
4. **`free` exige la dirección exacta** que devolvió un `alloc`.
5. **La página 0 es válida.** Un SO real la deja sin mapear para que desreferenciar
   `NULL` falle; aquí el enunciado la usa como dirección de trabajo.
6. **No hay TLB.** El capítulo 19 de OSTEP es parte del material del curso, y un TLB
   sería la extensión más natural: con una caché de traducciones, `t2_localidad`
   mostraría un *TLB hit rate* alto por su localidad espacial, independiente del hit
   rate de páginas.

---

## 9. Verificación y calidad

*(Criterio de la rúbrica: "Calidad de código" — 15 %)*

| Verificación | Resultado |
|---|---|
| `gcc -Wall -Werror -std=c99` | limpio |
| `clang -Wall -Werror -std=c99` | limpio |
| `-O0`, `-O2`, `-O3`, `-Os`, y con `-DNDEBUG` | limpio en gcc y clang |
| 26 flags de advertencia adicionales de gcc | limpio |
| `-Weverything` de clang | limpio salvo dos avisos de compatibilidad con C++ |
| `gcc -fanalyzer` (análisis estático), 8 archivos | sin hallazgos |
| `cppcheck --enable=all` | sin errores ni warnings |
| `valgrind --leak-check=full`, 72 corridas | **0 fugas, 0 errores** |
| ASan + UBSan + LeakSanitizer, 80 corridas | 0 hallazgos |
| Caminos de error y entradas hostiles | 0 fugas |
| Fallos de `calloc` inyectados (`-Wl,--wrap`), 19 puntos | 0 fugas; el *rollback* de `alloc` deja el espacio virtual intacto |

Salida de valgrind sobre la prueba más grande:

```
$ valgrind --leak-check=full ./simulador tests/t3_estres.txt
HEAP SUMMARY:
    in use at exit: 0 bytes in 0 blocks
  total heap usage: 10 allocs, 10 frees, 284,520 bytes allocated

All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

**Separación por responsabilidad**, como pide la rúbrica: parseo (`parser.c`),
traducción (`mmu.c`), tablas y `alloc`/`free` (`pagetable.c`), marcos (`physmem.c`),
manejo de fallos (`fallos.c`), reemplazo (`replace.c`), estadísticas (`stats.c`).
Sin variables globales: el estado del simulador se pasa por parámetro, y el de la
política es `static` privado de su módulo por exigencia de la interfaz.

---

## 10. Conclusiones

**[COMPLETAR — esta sección la escribe el grupo. Insumos medidos disponibles:]**

- La política de reemplazo no influye mientras el conjunto de trabajo quepa en
  memoria (`t1`, `t2`: 0 reemplazos).
- En un barrido cíclico mayor que la memoria, LRU y FIFO son igual de malas
  (`t3`: 200 fallos las dos), y falta un marco para caer del acantilado.
- LRU solo se distingue de FIFO cuando hay presión de memoria *y* páginas calientes
  antiguas; en la prueba construida para eso, la diferencia fue de 1 fallo.
- La tabla de dos niveles pasa de 12 400 B a 4 210 792 B según lo disperso que sea
  el uso del espacio virtual; una de un nivel costaría 4 194 304 B siempre.
- El hit rate no captura el costo de las páginas sucias: 136 de 136 desalojos de
  `t3` habrían implicado una escritura a disco.

## 11. Reparto del trabajo

**[COMPLETAR: qué hizo cada integrante.]**

## 12. Referencias

- Arpaci-Dusseau, R. y Arpaci-Dusseau, A. *Operating Systems: Three Easy Pieces*.
  Capítulo 18 (Paging: Introduction), 19 (TLBs), 20 (Advanced Page Tables),
  22 (Beyond Physical Memory: Policies).
- `docs/lab-spec.md` — enunciado del laboratorio.
- `docs/resultados.md` — tabla de resultados medidos.
