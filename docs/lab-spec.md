# Laboratorio: memoria virtual (enunciado)

Transcripción del PDF entregado por el profesor. Es la fuente de verdad del proyecto.

## Información general

- Grupos de máximo 3 estudiantes
- Lenguaje C/C++
- Fecha de entrega: miércoles 25 de septiembre, 23:59
- Formato de entrega: repositorio de GitHub con código, Makefile, README.md y reporte

## Contexto

Implementar un simulador de gestión de memoria virtual basado en paginación. Entender
cómo funciona la traducción VA → PA, la estructura de tablas de páginas, los fallos de
página, y cómo distintas políticas de reemplazo impactan el rendimiento. Cada grupo
implementa una política diferente (FIFO o LRU), lo que permite comparar resultados y
analizar trade-offs.

## Requisitos funcionales

1. **Estructura de memoria virtual**
   - Sistema de paginación con tablas de 2 niveles (como x86-64 simplificado)
   - Tamaño de página: 4 KB (configurable)
   - Espacio virtual: 32 bits máximo
   - Espacio físico: configurable, mínimo 256 KB

2. **Tabla de páginas de dos niveles**
   - Dirección virtual de 32 bits: `PT1 (10b) | PT2 (10b) | Offset (12b)`
   - PT1 (bits 31-22): índice en tabla nivel 1 (1024 entradas)
   - PT2 (bits 21-12): índice en tabla nivel 2 (1024 entradas)
   - Offset (bits 11-0): desplazamiento dentro de la página de 4 KB

3. **Traducción de direcciones**
   - Función de traducción VA → PA
   - PTE contiene: número de página física, bits valid, accessed, dirty
   - Fallo de página si la página no está en memoria
   - Crear tabla de nivel 2 dinámicamente si no existe

4. **Manejo de fallos de página**
   - Asignar página física disponible
   - Si no hay: aplicar política de reemplazo (FIFO o LRU)
   - Registrar estadísticas: número de fallos, número de reemplazos, tiempo

5. **Política de reemplazo (por grupo)**
   - FIFO: reemplazar la página más antigua (simple, determinista)
   - LRU: reemplazar la página no usada más recientemente (complejo, mejor hit rate)

## Entrada y salida

El programa acepta un archivo de entrada con comandos:

```
alloc <bytes>
write <virtual_addr> <value>
read <virtual_addr>
free <virtual_addr>
```

Ejemplo:

```
alloc 8192
write 0 42
write 4096 99
read 0
read 4096
```

Salida (estadísticas finales):

- Total de accesos: N
- Total fallos de página: M
- Hit rate: (N-M)/N * 100%
- Total reemplazos: K
- Política: FIFO o LRU

## Requisitos técnicos

- Compilar sin warnings: `gcc -Wall -Werror -std=c99`
- Makefile con targets: `all`, `clean`, `run`
- Código comentado y bien estructurado
- Funciones separadas: traducción, fallos, reemplazo
- Sin memory leaks (valgrind)

## Entregables

- Código fuente (.c/.cpp y .h/.hpp/.hcc)
- Makefile funcional
- README.md con instrucciones de compilación y uso
- Reporte de análisis (PDF o markdown) con:
  - Descripción de estructuras de datos
  - Explicación de la política de reemplazo
  - Resultados de 2-3 programas de prueba
  - Análisis: cambio de hit rate, número de reemplazos
  - Comparación teórica con otra política

## Rúbrica de evaluación

| Criterio | Peso | Qué evalúan |
|---|---|---|
| Funcionalidad core | 35% | Traducción VA→PA correcta, manejo de fallos, tabla de 2 niveles funcionando |
| Política de reemplazo | 25% | Implementación correcta de FIFO o LRU según asignación |
| Calidad de código | 15% | Estructurado, comentado, sin memory leaks, sin warnings |
| Reporte de análisis | 15% | Explicación clara, pruebas concretas, análisis crítico |
| Bonus: optimizaciones | +10% | Políticas adicionales, visualización, stress testing |

## Recursos

- OSTEP: capítulos 18 (Paging), 19 (TLB), 20 (Advanced Page Tables)
- C Reference: man pages para struct, malloc, memcpy
- Debugging: valgrind para memory leaks
