# Reporte de análisis — Simulador de memoria virtual con paginación de dos niveles

**Curso:** Sistemas Operativos, Universidad de Antioquia, 2026-2
**Integrantes:** 
* Cristian Alejandro Carvajal Mellizo
* Jhomar Farid Arrieta Montes

**Política principal implementada:** LRU. **Política adicional:** FIFO.

Todas las cifras de este documento están medidas con el código de este
repositorio y se reproducen con los comandos indicados en cada sección.

---

## 1. El modelo simulado

El simulador **no** reserva memoria del sistema operativo para el proceso simulado:
modela con estructuras de datos en C un espacio virtual, una memoria física, un área
de intercambio (swap) y las tablas de páginas que los relacionan.

| Parámetro | Valor |
|---|---|
| Tamaño de página | 4 KB por defecto; configurable al compilar con `make PAGE_BITS=10..16` (1 KB a 64 KB) |
| Espacio virtual | 32 bits = 4 GB; 2²⁰ = 1 048 576 páginas con 4 KB |
| Memoria física | configurable con `-m`, por defecto 256 KB = **64 marcos**, máximo 1 GB |
| Niveles de tabla | 2 (directorio + tablas de nivel 2) |
| Unidad de acceso | palabra de 32 bits, dirección alineada a 4 bytes |
| Swap | ilimitado; guarda las páginas sucias que se desalojan |

Dirección virtual con páginas de 4 KB:

```
 31            22 21            12 11                 0
+----------------+----------------+-------------------+
|   PT1 (10 b)   |   PT2 (10 b)   |   offset (12 b)   |
+----------------+----------------+-------------------+
```

### 1.1 Cómo se deriva el formato cuando cambia la página

`src/config.h` recibe `BITS_OFFSET` (el Makefile lo pasa como `-DBITS_OFFSET=$(PAGE_BITS)`)
y calcula los otros dos campos para que la dirección siempre sume 32 bits:

```c
#define BITS_NIVEL2 (BITS_OFFSET - 2)                          /* PTE de 4 B */
#define BITS_NIVEL1 (BITS_DIRECCION - BITS_OFFSET - BITS_NIVEL2)
#define BITS_PFN    (BITS_DIRECCION - BITS_OFFSET)             /* PA de 32 bits */
```

PT2 usa `BITS_OFFSET − 2` bits porque una página contiene `TAM_PAGINA / 4` PTEs de
4 bytes: así cada tabla de nivel 2 ocupa exactamente una página, que es la regla que
OSTEP cap. 20.3 usa para dimensionar una tabla multinivel ("make each piece of the
page table fit within a single page"). PT1 recibe los bits que sobran.

| Página | PT1 | PT2 | Offset | Entradas del directorio | PTEs por tabla L2 |
|---|---|---|---|---|---|
| 1 KB | 14 b | 8 b | 10 b | 16 384 | 256 |
| 4 KB | **10 b** | **10 b** | **12 b** | **1 024** | **1 024** |
| 16 KB | 6 b | 12 b | 14 b | 64 | 4 096 |
| 64 KB | 2 b | 14 b | 16 b | 4 | 16 384 |

Un `#error` impide compilar fuera del rango 10..16, y el Makefile recompila todo
cuando cambia `PAGE_BITS`.

La extracción de campos usa máscaras derivadas del formato (`src/mmu.c`):

```c
uint32_t mmu_pt1(uint32_t va)    { return (va >> (BITS_NIVEL2 + BITS_OFFSET)) & MASCARA_NIVEL1; }
uint32_t mmu_pt2(uint32_t va)    { return (va >> BITS_OFFSET) & MASCARA_NIVEL2; }
uint32_t mmu_offset(uint32_t va) { return va & MASCARA_OFFSET; }
```

y la dirección física se arma dejando el offset intacto: `pa = (pfn << BITS_OFFSET) | offset`.

### 1.2 Verificación de la traducción (página de 4 KB)

| VA | PT1 | PT2 | offset | Comentario |
|---|---|---|---|---|
| `0x00001004` | 0 | 1 | 4 | página 1, offset 4 |
| `0x00400000` | 1 | 0 | 0 | primera VA de la segunda región de 4 MB |
| `0x00000FFF` | 0 | 0 | 4095 | último byte de la primera página |
| `0x003FFFFF` | 0 | 1023 | 4095 | último byte de la primera región de 4 MB |
| `0xFFFFFFFF` | 1023 | 1023 | 4095 | última dirección del espacio |

Con `pfn = 3`, la VA `0x00001004` produce la PA `0x00003004`: cambia la parte alta
(página 1 → marco 3) y el offset `0x004` se conserva. `./simulador <archivo> -v`
imprime la PA de cada acceso, y `make test` verifica que `write 4096 99` en el
ejemplo del enunciado use el marco 1 y la PA `0x00001000`.

---

## 2. Descripción de las estructuras de datos

### 2.1 La PTE: `pte_t` — 4 bytes

```c
typedef struct {
    unsigned int pfn      : BITS_PFN; /* marco fisico: 20 bits con paginas de 4 KB */
    unsigned int valid    : 1;  /* la pagina fue asignada por el proceso */
    unsigned int present  : 1;  /* la pagina esta en memoria fisica */
    unsigned int accessed : 1;  /* referenciada por un read o un write */
    unsigned int dirty    : 1;  /* modificada por un write desde que se cargo */
    unsigned int swapped  : 1;  /* hay una copia de la pagina en el swap */
} pte_t;
```

Contiene lo que exige el enunciado (número de página física, `valid`, `accessed`,
`dirty`) más `present` y `swapped`. Se usan campos de bits porque así es una PTE real
de x86 de 32 bits, donde los bits de control viven en la misma palabra que el número
de marco (OSTEP cap. 18.3), y porque la entrada mide 4 bytes. `src/pagetable.c`
verifica en tiempo de compilación que `sizeof(pte_t) == 4`.

El campo `pfn` cambia de significado según el estado de la página:

| `present` | `swapped` | `pfn` contiene |
|---|---|---|
| 1 | — | el número de marco físico |
| 0 | 1 | el número de *slot* en el swap |
| 0 | 0 | nada: la página nunca se escribió y vuelve en ceros |

Guardar la dirección en disco en los bits del PFN es la técnica que describe OSTEP
cap. 21.3 ("the OS could use the bits in the PTE normally used for data such as the
PFN of the page for a disk address").

### 2.2 Tabla de nivel 2: `tabla_nivel2_t` — 4 104 bytes

1024 PTEs (4 096 B) más un contador `paginas_validas`, que permite detectar que la
tabla quedó sin páginas válidas para liberarla con `free()`.

### 2.3 Directorio: `directorio_t` — 8 232 bytes

1024 punteros a tablas de nivel 2 más el contador de tablas vivas, el puntero del
espacio virtual y el registro de asignaciones. Es un **arreglo fijo de punteros**: el
índice PT1 *es* la posición, así que la consulta es un acceso directo, y `NULL`
codifica "esta región de 4 MB no tiene ninguna página válida" (el PDE inválido de
OSTEP cap. 20.3).

### 2.4 Marcos: `marco_t` — 24 bytes, y la lista de libres

```c
typedef struct {
    int      ocupado;
    uint32_t vpn;       /* pagina virtual alojada: el mapeo inverso */
    int      siguiente; /* siguiente marco de la lista de libres, o -1 */
    long     slot_swap; /* slot con copia de esta pagina, o -1 */
} marco_t;
```

- `vpn` es el **mapeo inverso**: al desalojar una víctima hay que poner `present = 0`
  en *su* PTE, y el marco necesita saber de qué página es. Se guarda el número y no un
  `pte_t *` para no quedar con un puntero colgante si `free` libera la tabla de nivel 2.
- `slot_swap`: mientras la página está en memoria, `pfn` contiene el marco, así que el
  slot de su copia en swap (si tiene) se recuerda aquí. Al desalojarla vuelve al `pfn`.
- La **lista de libres es intrusiva**: índice de cabeza más el campo `siguiente`, O(1)
  al tomar y al devolver, sin un `malloc` por nodo.

El contenido de la memoria física es un bloque contiguo de `num_marcos × TAM_PAGINA`
bytes. `read` y `write` trabajan con **palabras de 32 bits** copiadas con `memcpy`, y
la dirección debe ser múltiplo de 4: así una palabra nunca cruza el límite entre dos
páginas y un acceso es exactamente una traducción. Al entregar un marco nuevo se pone
en ceros (*demand zeroing*).

### 2.5 Área de swap: `area_swap_t` (`src/swap.c`)

```c
typedef struct {
    unsigned char **paginas;   /* paginas[slot]: copia de TAM_PAGINA bytes */
    uint32_t       *libres;    /* pila de slots liberados, para reutilizarlos */
    size_t          num_slots, en_uso, pico;
    ...
} area_swap_t;
```

Un arreglo dinámico de slots del tamaño de una página y una pila de slots libres. Los
arreglos crecen duplicando su capacidad (costo amortizado O(1)), y la pila de libres
se agranda por adelantado al reservar, para que `swap_liberar` nunca necesite memoria
y no pueda fallar.

### 2.6 Registro de asignaciones: `asignacion_t` — 8 bytes

`free <dir>` libera la asignación **completa**, igual que `free(ptr)` en C, así que el
directorio guarda un arreglo dinámico de `{vpn_inicio, paginas}`.

### 2.7 Resumen de tamaños (medidos con `sizeof`, visibles con `-v`)

| Estructura | Tamaño | Cuántas hay |
|---|---|---|
| `pte_t` | **4 B** | 1024 por tabla de nivel 2 |
| `tabla_nivel2_t` | **4 104 B** | solo las regiones de 4 MB con páginas válidas |
| `directorio_t` | **8 232 B** | una, desde el arranque |
| `marco_t` | **24 B** | una por marco (1 536 B con 64 marcos) |
| `asignacion_t` | **8 B** | una por `alloc` vivo |
| memoria física completa | **263 712 B** | 256 KB de datos + metadatos |

### 2.8 El ahorro de la tabla de dos niveles

| Escenario | Memoria de traducción |
|---|---|
| Tabla de **un solo nivel** para 2²⁰ páginas | **4 194 304 B, siempre** |
| Dos niveles, `t1_basico` (toca 1 región de 4 MB) | **12 400 B** |
| Dos niveles, `alloc` del espacio completo de 4 GB | **4 210 792 B** |

La primera fila se paga completa aunque el proceso use una sola página; la segunda
es unas 338 veces menor. La tercera es el peor caso: cuando el proceso asigna **todo**
el espacio virtual, la tabla de dos niveles cuesta lo mismo que la de un nivel más el
directorio. La tabla multinivel ahorra memoria **en espacios virtuales dispersos**,
que es el caso de cualquier proceso real (OSTEP cap. 20.3).

### 2.9 Efecto del tamaño de página

`t4_localidad_8020.txt` con 256 KB de memoria física, compilando con distintos
`PAGE_BITS` (`make PAGE_BITS=<n>` y `./simulador tests/t4_localidad_8020.txt -v`):

| Página | Marcos | Memoria de traducción | Fallos LRU | Fallos FIFO |
|---|---|---|---|---|
| 1 KB | 256 | 133 240 B | 601 | 745 |
| 4 KB | 64 | **12 400 B** | **486** | **660** |
| 16 KB | 16 | 17 008 B | 463 | 638 |
| 64 KB | 4 | 65 680 B | 540 | 701 |

Dos efectos opuestos:

- **Memoria de traducción.** Con 1 KB el directorio tiene 2¹⁴ entradas y ocupa 128 KB:
  es el problema que OSTEP cap. 20.3 resuelve agregando un tercer nivel ("our goal of
  making every piece of the multi-level page table fit into a page vanishes"). Con
  64 KB las tablas de nivel 2 son de 64 KB cada una. 4 KB queda en el mínimo.
- **Fallos.** Páginas más grandes traen más datos por fallo (localidad espacial),
  pero con la memoria fija hay menos marcos. Con 64 KB solo quedan 4 marcos y los
  fallos vuelven a subir.

---

## 3. Traducción, fallos de página y swap

`mmu_traducir` recorre los dos niveles y devuelve uno de tres resultados:

| Estado de la PTE | Resultado | Qué hace el simulador | Contadores |
|---|---|---|---|
| Sin tabla de nivel 2, o `valid = 0` | `TRAD_SEGFAULT` | **Acceso ilegal**. No trae nada a memoria | `ilegales++`; **no** cuenta como acceso |
| `valid = 1, present = 0` | `TRAD_FALLO_PAGINA` | **Fallo de página** (abajo) y completa el acceso | `accesos++` y `fallos++` |
| `valid = 1, present = 1` | `TRAD_OK` | Acierto: arma la PA y toca el marco | `accesos++` |

`valid = 0` significa que la página **nunca se asignó**: es el *segmentation fault* de
un programa real. `valid = 1, present = 0` significa que la página existe pero está
fuera de memoria. Los ilegales van en un contador aparte para mantener
`accesos = hits + fallos`. Un `read`/`write` a una dirección que no es múltiplo de 4
tampoco cuenta como acceso (contador `no_alineados`).

### 3.1 Manejo del fallo (`src/fallos.c`, `atender_fallo`)

1. **Asignar un marco libre** (`physmem_tomar_marco`).
2. **Si no hay**, `desalojar_victima` pide la víctima a la política y:
   - si la víctima está sucia (`dirty = 1`), la escribe en el swap: reutiliza su slot
     si ya tenía uno, o reserva uno nuevo;
   - si está limpia y tiene slot, esa copia sigue vigente; si no tiene slot, nunca se
     escribió y no hace falta guardarla;
   - pone `present = 0`, `swapped` según corresponda y el slot en `pfn`, y devuelve
     el marco a la lista de libres.
3. **Cargar la página:** si `swapped = 1`, copia el slot al marco; si no, el marco
   queda en ceros.
4. Actualiza la PTE (`pfn = marco`, `present = 1`, `dirty = 0`) y avisa a la política
   (`politica_al_cargar`).

Al desalojar, `valid` se queda en 1: la página no deja de existir, solo deja de estar
en memoria. Si el proceso la vuelve a tocar es otro fallo de página, no un acceso
ilegal.

Verificación (`make test`, prueba `t5_swap.txt`): se escribe `k × 1000 + 7` en 80
páginas —16 más que los 64 marcos— y al releerlas las 80 devuelven su valor, con LRU
y con FIFO, a pesar de que todas pasaron por el swap.

### 3.2 Cuándo se crea una tabla de nivel 2

El enunciado pide "crear tabla nivel 2 dinámicamente si no existe". En este simulador
la tabla se crea **en `alloc`**, la primera vez que una asignación toca su región de
4 MB, y no en la traducción. La razón es de diseño: el bit `valid`, que distingue un
acceso ilegal de un fallo de página, vive en la PTE, y para marcar las páginas de una
asignación como válidas su PTE tiene que existir. La creación sigue siendo dinámica
—una región sin asignaciones nunca tiene tabla, y `free` destruye las tablas que
quedan sin páginas válidas— y la traducción trata una tabla inexistente como PDE
inválido (acceso ilegal). La alternativa, crear la tabla en el primer fallo, exigiría
consultar el registro de asignaciones en cada fallo para saber si la dirección es
legal; el resultado observable (fallos, reemplazos, memoria de traducción) es el
mismo porque ninguna tabla se crea para una región que no se asignó.

`make test` lo verifica: después de `alloc 8192` y de un `alloc` de 8 MB (que empieza
en la VA 0x2000 y abarca las regiones PT1 = 0, 1 y 2) hay 3 tablas de nivel 2 vivas, y
el `free` de la segunda asignación deja solo la tabla que sigue usando la primera.

---

## 4. La política de reemplazo

### 4.1 La interfaz

La política vive detrás de `src/replace.h`, y **ningún otro archivo pregunta qué
política está activa**:

```c
typedef enum { POLITICA_LRU, POLITICA_FIFO } politica_t;

int         politica_init(politica_t p, int num_marcos);
void        politica_al_cargar(int marco);   /* una pagina entro a este marco */
void        politica_al_acceder(int marco);  /* en CADA acceso, hit o fallo   */
int         politica_elegir_victima(void);
void        politica_liberar(void);
const char *politica_nombre(void);
```

El estado vive como `static` dentro de `replace.c`: es estado privado del módulo,
creado por `politica_init` y liberado por `politica_liberar`.

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

- **FIFO**: la marca se pone solo al cargar → es el **orden de llegada**. Un acierto no
  reordena nada: la página sale por antigüedad, sin importar cuánto se use.
- **LRU**: la marca se pone al cargar **y en cada acceso** → es el **último uso**.

`politica_al_acceder` se llama desde `fallos.c` al final de *todo* acceso completado,
hit o fallo. Si solo se llamara en los fallos, LRU nunca vería los aciertos y se
comportaría como FIFO.

### 4.3 Costo

| Operación | Costo | Frecuencia |
|---|---|---|
| `politica_al_cargar` | O(1) | una vez por fallo de página |
| `politica_al_acceder` | O(1) (FIFO: nada) | en **cada** acceso |
| `politica_elegir_victima` | **O(num_marcos)** | solo en un desalojo |
| Memoria | 8 B por marco (512 B con 64 marcos) | |

### 4.4 Por qué marcas y no una cola enlazada

La interfaz de `replace.h` no tiene un `politica_al_liberar`, así que cuando `free`
devuelve un marco a la lista de libres, la política no se entera. Con marcas eso se
corrige solo: al reusar el marco, `politica_al_cargar` sobrescribe su marca. Una cola
enlazada dejaría ese marco **duplicado** dentro de la cola y acabaría desalojando un
marco que ya no le pertenece a esa página. La prueba diferencial (§9) mezcla `free` con
memoria llena y confirma que no ocurre.

### 4.5 LRU exacto no es implementable en un SO real

Este simulador hace LRU **exacto**: actualiza una marca en cada acceso a memoria. Un
SO real no puede, porque el hardware tendría que escribir en una estructura del kernel
en *cada* referencia. Por eso se aproxima con el bit de referencia (`accessed`, OSTEP
cap. 18.3) y el **algoritmo del reloj** (OSTEP cap. 22.8). El simulador ya mantiene
`accessed` en cada acceso, así que el reloj es la extensión natural.

---

## 5. Resultados de las pruebas

Configuración: página de 4 KB, memoria física de 256 KB = **64 marcos**. Reproducible
con `./simulador tests/<archivo> -p <lru|fifo> -v`.

### 5.1 Las seis pruebas

| Prueba | Política | Accesos | Fallos | Hit rate | Reemplazos | Escrituras a swap | Lecturas de swap | AMAT |
|---|---|---|---|---|---|---|---|---|
| `t1_basico` | LRU | 4 | 2 | 50,00 % | 0 | 0 | 0 | 5 000,1 µs |
| `t1_basico` | FIFO | 4 | 2 | 50,00 % | 0 | 0 | 0 | 5 000,1 µs |
| `t2_localidad` | LRU | 60 | 4 | 93,33 % | 0 | 0 | 0 | 666,8 µs |
| `t2_localidad` | FIFO | 60 | 4 | 93,33 % | 0 | 0 | 0 | 666,8 µs |
| `t3_estres` | LRU | 400 | 200 | 50,00 % | 136 | 136 | 100 | 5 000,1 µs |
| `t3_estres` | FIFO | 400 | 200 | 50,00 % | 136 | 136 | 100 | 5 000,1 µs |
| `t4_localidad_8020` | LRU | 5 000 | **486** | **90,28 %** | **422** | **202** | 304 | 972,1 µs |
| `t4_localidad_8020` | FIFO | 5 000 | 660 | 86,80 % | 596 | 396 | 487 | 1 320,1 µs |
| `t5_swap` | LRU | 160 | 160 | 0,00 % | 96 | 80 | 80 | 10 000,1 µs |
| `t5_swap` | FIFO | 160 | 160 | 0,00 % | 96 | 80 | 80 | 10 000,1 µs |
| `t6_discriminante` | LRU | 71 | **65** | 8,45 % | **1** | 1 | 0 | 9 155,0 µs |
| `t6_discriminante` | FIFO | 71 | 66 | 7,04 % | 2 | 2 | 1 | 9 295,9 µs |

AMAT = tiempo medio de acceso, con 100 ns por acceso a memoria y 10 ms por fallo de
página, los valores de OSTEP cap. 22.1 ("the cost of accessing memory (TM) is around
100 nanoseconds, and the cost of accessing disk (TD) is about 10 milliseconds").

### 5.2 Qué hace cada prueba

- **`t1_basico`** — el ejemplo del enunciado: `alloc 8192` y cuatro accesos a dos
  páginas. Verifica la traducción y los fallos obligatorios. El mismo ejemplo escrito
  en una sola línea, como aparece en el PDF, da el mismo resultado.
- **`t2_localidad`** — 60 accesos sobre **4 páginas** con localidad temporal y espacial.
- **`t3_estres`** — `alloc` de 100 páginas y **dos pasadas** de barrido cíclico, cada
  página con un `write` seguido de un `read`. 100 páginas contra 64 marcos.
- **`t4_localidad_8020`** — 5 000 accesos sobre **100 páginas**: el 80 % va a 20 páginas
  calientes y el 20 % al resto; 30 % de escrituras. El conjunto de trabajo no cabe
  en memoria, pero tiene una parte caliente que sí.
- **`t5_swap`** — 80 páginas escritas y releídas: comprueba que el contenido sobrevive
  al desalojo.
- **`t6_discriminante`** — 64 páginas llenan la memoria, la página 0 (la más antigua)
  se lee 5 veces y llega una página nueva.

`t4`, `t5` y `t6` se generan con `bash tests/generar_pruebas.sh`, con un generador
pseudoaleatorio propio para que la traza sea idéntica en cualquier máquina.

### 5.3 Barrido del tamaño de memoria (`make barrido`)

**`t4_localidad_8020`** (100 páginas = 400 KB):

| Memoria | Marcos | Fallos LRU | Fallos FIFO | Hit rate LRU | Hit rate FIFO | Reemplazos LRU | Reemplazos FIFO |
|---|---:|---:|---:|---:|---:|---:|---:|
| 256 KB | 64 | 486 | 660 | 90,28 % | 86,80 % | 422 | 596 |
| 320 KB | 80 | 307 | 375 | 93,86 % | 92,50 % | 227 | 295 |
| 384 KB | 96 | 135 | 158 | 97,30 % | 96,84 % | 39 | 62 |
| 448 KB | 112 | 100 | 100 | 98,00 % | 98,00 % | 0 | 0 |

```
Fallos de pagina por tamano de memoria (el maximo mide 50 #):
  256 KB  LRU      486  #####################################
  256 KB  FIFO     660  ##################################################
  320 KB  LRU      307  #######################
  320 KB  FIFO     375  ############################
  384 KB  LRU      135  ##########
  384 KB  FIFO     158  ############
  448 KB  LRU      100  ########
  448 KB  FIFO     100  ########
```

**`estres_grande`** (`make stress`: 100 000 accesos 80/20 sobre 256 páginas = 1 MB):

| Memoria | Marcos | Fallos LRU | Fallos FIFO | Hit rate LRU | Hit rate FIFO |
|---|---:|---:|---:|---:|---:|
| 256 KB | 64 | 30 987 | 39 617 | 69,01 % | 60,38 % |
| 384 KB | 96 | 16 809 | 26 667 | 83,19 % | 73,33 % |
| 512 KB | 128 | 12 458 | 18 773 | 87,54 % | 81,23 % |
| 768 KB | 192 | 6 182 | 8 191 | 93,82 % | 91,81 % |
| 1024 KB | 256 | 256 | 256 | 99,74 % | 99,74 % |

**`t3_estres`** (100 páginas = 400 KB), igual con LRU y con FIFO:

| Memoria | Marcos | Fallos | Hit rate | Reemplazos | `fallos − marcos` |
|---|---|---|---|---|---|
| 256 KB | 64 | 200 | 50,00 % | 136 | 136 |
| 384 KB | 96 | 200 | 50,00 % | 104 | 104 |
| 396 KB | 99 | 200 | 50,00 % | 101 | 101 |
| 400 KB | 100 | **100** | **75,00 %** | **0** | — |
| 512 KB | 128 | 100 | 75,00 % | 0 | — |

---

## 6. Análisis del hit rate y de los reemplazos

### 6.1 `t1` y `t2` alcanzan el mínimo teórico

En `t1` (2 fallos para 2 páginas) y en `t2` (4 fallos para 4 páginas) **todos** los
fallos son obligatorios: ninguna política, ni siquiera la óptima, podría bajar de ahí.
Hay **0 reemplazos** porque 2 y 4 páginas caben en 64 marcos, así que la política
nunca se consulta. **La política de reemplazo es irrelevante mientras el conjunto de
trabajo quepa en memoria.**

### 6.2 `t3`: 136 reemplazos, y por qué LRU y FIFO empatan

**Primera pasada — 100 fallos.** El `write` de cada página falla y el `read` inmediato
acierta. Los primeros 64 fallos usan marcos libres; los 36 restantes (páginas 64–99)
necesitan víctima → **36 reemplazos**.

**Segunda pasada — otros 100 fallos.** En memoria están las páginas 36–99. Llega la
página 0 → fallo, se desaloja la 36. Llega la 1 → se desaloja la 37. Cuando el barrido
llega a la página 36, fue expulsada hace 36 accesos. **Todas fallan** → **100
reemplazos**. Total: 36 + 100 = **136**. Las 100 páginas de la segunda pasada se traen
del swap (100 lecturas de swap), porque todas se escribieron en la primera.

En un recorrido cíclico "la página que llegó primero" (FIFO) y "la que se usó hace más
tiempo" (LRU) **son la misma página**, así que empatan. OSTEP cap. 22 muestra el mismo
resultado para su *looping-sequential workload*: LRU y FIFO obtienen 0 % de aciertos
con una caché una página menor que el ciclo.

### 6.3 El acantilado de `t3`

Con **99 marcos para 100 páginas** el resultado es idéntico al de 64 marcos: 200
fallos. Con 100 marcos cae de golpe a los 100 fallos obligatorios. No hay mejora
gradual: mientras falte un marco, la página que se desaloja es siempre la que se va a
necesitar enseguida. El 50 % de `t3` es engañoso: sale de que cada página se toca dos
veces seguidas (`write` y `read`); el 100 % de los *primeros* accesos a cada página
fallan en las dos pasadas.

### 6.4 `t4` y `estres_grande`: donde las políticas se separan

Con localidad 80/20 y memoria insuficiente, LRU gana en todas las configuraciones:

- **`t4` con 64 marcos**: 486 fallos contra 660 (**26 % menos**), 422 reemplazos contra
  596. El hit rate pasa de 86,80 % a 90,28 %.
- **`estres_grande` con 64 marcos**: 30 987 fallos contra 39 617 (**22 % menos**); el
  hit rate sube casi 9 puntos (60,38 % → 69,01 %).

La razón es la misma en los dos casos: LRU conserva las 20 páginas calientes porque se
usan todo el tiempo; FIFO las desaloja cada vez que llegan a la cabeza de la cola, sin
importar su uso, y tiene que volver a traerlas.

**Cómo cambia la diferencia con la memoria.** En `t4` la brecha se cierra al crecer la
memoria (174 fallos de diferencia con 64 marcos, 68 con 80, 23 con 96) y desaparece
con 112 marcos, cuando las 100 páginas caben. En `estres_grande` la brecha es máxima con
80 marcos (10 456 fallos) y también desaparece cuando las 256 páginas caben. **La política
importa exactamente en la zona donde la memoria es escasa respecto al conjunto de
trabajo, pero suficiente para la parte caliente.**

### 6.5 `t6`: el caso mínimo

```
LRU   [linea  72] write 0x00040000 = 64 -> FALLO marco  1, PA 0x00001000, desaloja la pagina 0x00001
      [linea  73] read  0x00000000 = 0  -> hit   marco  0, PA 0x00000000
FIFO  [linea  72] write 0x00040000 = 64 -> FALLO marco  0, PA 0x00000000, desaloja la pagina 0x00000
      [linea  73] read  0x00000000 = 0  -> FALLO marco  1, PA 0x00001000, traida del swap, desaloja la pagina 0x00001
```

La página 0 era la **más antigua** pero se acababa de usar 5 veces. LRU la protegió;
FIFO la desalojó y tuvo que traerla del swap: un fallo, un reemplazo y una escritura a
swap más.

### 6.6 El tiempo: lo que el hit rate no deja ver

El simulador reporta dos tiempos distintos:

- **Tiempo de ejecución (CPU)**: lo que tardó el simulador, medido con `clock()`. Para
  `t3` es menor a 1 ms y para `estres_grande` del orden de 100 ms. Solo dice que el
  simulador es rápido.
- **Tiempo simulado**: 100 ns por acceso y 10 ms por fallo (OSTEP cap. 22.1).

| Prueba | Hit rate | AMAT | Veces más lento que un acierto (0,1 µs) |
|---|---|---|---|
| `t2_localidad` | 93,33 % | 666,8 µs | ~6 668× |
| `t4` LRU | 90,28 % | 972,1 µs | ~9 721× |
| `t4` FIFO | 86,80 % | 1 320,1 µs | ~13 201× |

Un hit rate de 93 % suena excelente y aun así cada acceso cuesta en promedio 6 668
veces lo que costaría sin fallos. Como un fallo cuesta 100 000 veces más que un
acierto, el hit rate de la paginación tiene que ser altísimo, no "bueno". En `t4`, los
3,5 puntos de hit rate que separan a LRU de FIFO equivalen a un 26 % menos de tiempo
simulado (4 860 ms contra 6 600 ms).

### 6.7 El costo oculto: las páginas sucias

Cada desalojo de una página sucia es una escritura al swap, que el hit rate no
refleja. En `t4`, LRU hace **202** escrituras a swap y FIFO **396**: FIFO casi duplica
la E/S de escritura, más que la diferencia en fallos (660/486 = 1,36×). La razón es que
FIFO desaloja páginas calientes, que son justamente las que más se escriben, y cada
vez que vuelven se ensucian de nuevo. En `estres_grande` la relación es 20 731 contra
15 226 (1,36×).

Con el modelo de 10 ms por operación de disco, las escrituras de `t4` suman 2 020 ms
con LRU y 3 960 ms con FIFO, que no están incluidos en el tiempo simulado. El hit rate
es una métrica necesaria pero no suficiente: dos políticas con hit rates parecidos
pueden tener costos de E/S muy distintos.

También hay fallos que no leen del swap: en `t4` con LRU, de 486 fallos solo 304 se
atienden desde swap. Los otros 182 son los 100 primeros accesos y páginas que se
desalojaron limpias sin haberse escrito nunca, que vuelven en ceros sin E/S.

---

## 7. Comparación LRU vs FIFO

### 7.1 Lo que muestran los datos

| Prueba | Resultado | Por qué |
|---|---|---|
| `t1`, `t2` | empate | el conjunto de trabajo cabe: **cero reemplazos** |
| `t3` | empate (200 fallos) | barrido cíclico: FIFO y LRU eligen la misma víctima |
| `t5` | empate (160 fallos) | cada página se toca una vez por pasada: no hay reuso que aprovechar |
| `t4` | **LRU: 486 vs 660 fallos** | localidad 80/20 con memoria escasa |
| `estres_grande` | **LRU: 30 987 vs 39 617 fallos** | ídem, 100 000 accesos |
| `t6` | **LRU: 65 vs 66 fallos** | página antigua pero caliente |

En **ninguna** corrida LRU salió peor que FIFO.

### 7.2 La comparación teórica

Según OSTEP cap. 22, el comportamiento relativo depende de la carga de trabajo:

- **Sin localidad** (accesos uniformes): "LRU, FIFO, and Random all perform the same".
  No hay patrón que una política pueda aprovechar mejor que otra.
- **Con localidad 80/20**: **LRU gana**, porque conserva el 20 % caliente. `t4` y
  `estres_grande` lo reproducen.
- **Recorrido cíclico mayor que la memoria**: **las dos se hunden**. OSTEP muestra que
  Random incluso las supera en este caso, porque su aleatoriedad rompe la cadena de
  desalojos equivocados. `t3` es este caso.

Además, FIFO puede sufrir la **anomalía de Belady** (más memoria, más fallos), mientras
que LRU tiene la propiedad de pila: "a cache of size N + 1 naturally includes the
contents of a cache of size N" (OSTEP cap. 22.3), así que con LRU agregar marcos nunca
aumenta los fallos. En todos los barridos de §5.3 los fallos de las dos políticas
bajan o se mantienen al agregar memoria; ninguna de estas cargas produce la anomalía
en FIFO.

**Costo de implementación**, la otra mitad del trade-off:

| | FIFO | LRU |
|---|---|---|
| Trabajo en un acierto | **ninguno** | actualizar el último uso |
| Estructura mínima | orden de llegada | marca de último uso por marco |
| En este simulador | 8 B/marco, O(1) / O(n) | igual |
| **En un SO real** | trivial | **impagable en su forma exacta** |

### 7.3 Conclusión de la comparación

Con los datos medidos, para este simulador conviene **LRU**: nunca es peor que FIFO, y
en las cargas con localidad —las que se parecen a programas reales— reduce los fallos
entre 22 % y 26 % y las escrituras a swap entre 27 % y 49 %. La respuesta depende de la
carga: para una carga como `t2_localidad`, donde todo cabe en memoria, las dos son
equivalentes y FIFO tendría la ventaja de no hacer nada en los aciertos. En un sistema
real no se elegiría LRU exacto sino una aproximación barata como el reloj, que captura
buena parte de la ventaja de LRU usando solo el bit `accessed`.

---

## 8. Limitaciones del modelo

Decisiones de modelado; se declaran para que los resultados se lean correctamente.

1. **El tamaño de página se elige al compilar** (`make PAGE_BITS=n`), no con un flag en
   tiempo de ejecución, porque las tablas son arreglos de tamaño fijo.
2. **Palabras de 32 bits alineadas.** `read`/`write` trabajan con palabras en
   direcciones múltiplo de 4; una dirección no alineada se rechaza y se cuenta aparte.
3. **El espacio virtual no se reutiliza.** `alloc` reparte desde la dirección 0 con un
   puntero que solo sube, como `brk`/`sbrk`.
4. **`free` exige la dirección exacta** que devolvió un `alloc`.
5. **La página 0 es válida.** El enunciado la usa como dirección de trabajo.
6. **El swap vive en la memoria del simulador** y no tiene límite de tamaño.
7. **No hay TLB.** Con una caché de traducciones (OSTEP cap. 19), `t2_localidad`
   mostraría un *TLB hit rate* alto por su localidad espacial.
8. **LRU es exacto**, algo que el hardware real no permite (§4.5).

---

## 9. Verificación y calidad

| Verificación | Resultado |
|---|---|
| `gcc -Wall -Werror -std=c99` (gcc 13.3) | limpio |
| `gcc -Wall -Wextra -Wpedantic -Werror -std=c99` | limpio |
| `clang -Wall -Wextra -Wpedantic -Werror -std=c99` (clang 18) | limpio |
| `gcc -fanalyzer` | sin hallazgos |
| Compilación con `PAGE_BITS` 10 a 16 y con `-DNDEBUG` | limpio |
| `make test` | todas las verificaciones pasan |
| `make valgrind` (6 pruebas × 2 políticas) | 0 fugas, 0 errores |
| AddressSanitizer + UndefinedBehaviorSanitizer | 0 hallazgos |
| `make diferencial`: 200 trazas aleatorias × 2 políticas | 0 diferencias |

**Prueba diferencial** (`tests/diferencial.py`). Genera trazas aleatorias con varios
`alloc`, accesos con localidad, `free` a mitad de la traza, accesos a regiones ya
liberadas y, a veces, todos los comandos en una sola línea. Compara el simulador contra
un modelo de referencia escrito aparte en Python: total de accesos, fallos,
reemplazos y **el valor devuelto por cada `read`**. Para comprobar que la prueba
detecta errores se inyectaron dos a mano: desactivar la escritura al swap (9 de 40
corridas con diferencias) y hacer que LRU no actualice la marca en los aciertos (5 de
40). Las dos quedaron detectadas.

**Separación por responsabilidad**: parseo (`parser.c`), traducción (`mmu.c`), tablas
y `alloc`/`free` (`pagetable.c`), marcos (`physmem.c`), swap (`swap.c`), manejo de
fallos (`fallos.c`), reemplazo (`replace.c`), estadísticas (`stats.c`).

---

## 10. Conclusiones

- **La tabla de dos niveles cumple su propósito en espacios dispersos**: 12 400 B para
  `t1` contra 4 194 304 B de una tabla lineal. Cuando el proceso asigna todo el espacio
  virtual, el ahorro desaparece.
- **El tamaño de página es un compromiso**: con 256 KB de memoria, 4 KB minimiza la
  memoria de traducción; páginas más grandes reducen fallos hasta que quedan tan pocos
  marcos que vuelven a subir (64 KB: 4 marcos, 540 fallos).
- **La política no importa mientras el conjunto de trabajo quepa en memoria** (`t1`,
  `t2`: 0 reemplazos) **ni en un barrido cíclico mayor que la memoria** (`t3`: las dos
  fallan en el 100 % de los primeros accesos de la segunda pasada).
- **Con localidad y memoria escasa, LRU es claramente mejor**: 22–26 % menos fallos y
  27–49 % menos escrituras a swap que FIFO en `estres_grande` y `t4`.
- **El hit rate por sí solo engaña**: 93,33 % de aciertos en `t2` todavía significa un
  acceso promedio 6 668 veces más lento que sin fallos, y no refleja las escrituras de
  páginas sucias.
- **Sin swap los resultados de un simulador son incompletos**: una página sucia
  desalojada debe escribirse y recuperarse; `t5` verifica que el dato sobrevive.


## 11. Referencias

- Arpaci-Dusseau, R. y Arpaci-Dusseau, A. *Operating Systems: Three Easy Pieces*.
  Capítulos 18 (Paging: Introduction), 19 (Paging: Faster Translations (TLBs)),
  20 (Paging: Smaller Tables), 21 (Beyond Physical Memory: Mechanisms) y
  22 (Beyond Physical Memory: Policies).
