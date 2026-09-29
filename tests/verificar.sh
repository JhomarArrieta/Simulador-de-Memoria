#!/usr/bin/env bash
# verificar.sh - pruebas automaticas del simulador. Uso: make test
# Cada verificacion compara la salida contra un valor calculado a mano o contra
# una propiedad que debe cumplirse siempre. Termina con codigo != 0 si alguna falla.
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=./simulador
fallas=0

verificar() {  # verificar <descripcion> <condicion bash>
  if eval "$2"; then
    echo "  [OK]    $1"
  else
    echo "  [FALLA] $1"
    fallas=$((fallas + 1))
  fi
}

valor() {  # valor <salida> <etiqueta>: numero despues de "<etiqueta>: "
  grep -m1 "^$2:" <<< "$1" | sed 's/^[^:]*: *//; s/[^0-9.].*//'
}

echo "== Traduccion y tabla de dos niveles (t1, ejemplo del enunciado)"
salida=$($BIN tests/t1_basico.txt -v 2>&1)
verificar "4 accesos, 2 fallos, 0 reemplazos" \
  '[ "$(valor "$salida" "Total de accesos")/$(valor "$salida" "Total fallos de página")/$(valor "$salida" "Total reemplazos")" = "4/2/0" ]'
verificar "read 0 devuelve 42 y read 4096 devuelve 99" \
  '[ "$(grep -o "read  0x[0-9A-F]* = [0-9]*" <<< "$salida" | awk "{print \$4}" | tr "\n" " ")" = "42 99 " ]'
verificar "VA 0x00001000 -> marco 1, PA 0x00001000 (offset intacto)" \
  'grep -q "write 0x00001000 = 99 .*marco  1, PA 0x00001000" <<< "$salida"'

echo "== Formato de entrada"
una_linea=$(echo "alloc 8192 write 0 42 write 4096 99 read 0 read 4096" | $BIN /dev/stdin 2>&1 | head -4)
por_lineas=$($BIN tests/t1_basico.txt 2>&1 | head -4)
verificar "el ejemplo en una sola linea da lo mismo que un comando por linea" '[ "$una_linea" = "$por_lineas" ]'

echo "== Tablas de nivel 2 bajo demanda y liberacion con free"
salida=$(printf 'read 0\nalloc 8192\nalloc 8388608\nwrite 0x00400000 7\nfree 8192\nread 0x00400000\nread 0\n' \
         | $BIN /dev/stdin -v 2>&1)
verificar "acceso antes de alloc y despues de free son ilegales (2), no fallos" \
  'grep -q "accesos ilegales (valid=0)   : 2" <<< "$salida"'
verificar "alloc de 8 MB crea 3 tablas de nivel 2 y free deja solo 1" \
  'grep -q "tablas nivel 2: 3" <<< "$salida" && grep -q "tablas de nivel 2 vivas      : 1" <<< "$salida"'

echo "== Swap: los datos sobreviven al desalojo (t5)"
for p in lru fifo; do
  salida=$($BIN tests/t5_swap.txt -p "$p" -v 2>&1)
  malos=$(grep "read  0x" <<< "$salida" | awk '{ if ($6 != (NR - 1) * 1000 + 7) m++ } END { print m + 0 }')
  verificar "$p: las 80 lecturas devuelven k*1000+7" '[ "$malos" = "0" ]'
done
salida=$(printf 'alloc 4096\nwrite 0 300\nwrite 4 4294967295\nread 0\nread 4\nread 2\n' | $BIN /dev/stdin -v 2>&1)
verificar "palabras de 32 bits: 300 y 4294967295 se leen completos" \
  'grep -q "read  0x00000000 = 300 " <<< "$salida" && grep -q "read  0x00000004 = 4294967295 " <<< "$salida"'
verificar "acceso no alineado se rechaza y no cuenta como acceso" \
  'grep -q "no alineados a 4 B   : 1" <<< "$salida" && [ "$(valor "$salida" "Total de accesos")" = "4" ]'

echo "== Politicas de reemplazo"
lru=$($BIN tests/t6_discriminante.txt -p lru); fifo=$($BIN tests/t6_discriminante.txt -p fifo)
verificar "t6: LRU 65 fallos / 1 reemplazo, FIFO 66 / 2" \
  '[ "$(valor "$lru" "Total fallos de página")/$(valor "$lru" "Total reemplazos")/$(valor "$fifo" "Total fallos de página")/$(valor "$fifo" "Total reemplazos")" = "65/1/66/2" ]'
lru=$($BIN tests/t4_localidad_8020.txt -p lru); fifo=$($BIN tests/t4_localidad_8020.txt -p fifo)
verificar "t4 (80/20): LRU tiene menos fallos que FIFO" \
  '[ "$(valor "$lru" "Total fallos de página")" -lt "$(valor "$fifo" "Total fallos de página")" ]'
lru=$($BIN tests/t3_estres.txt -p lru); fifo=$($BIN tests/t3_estres.txt -p fifo)
verificar "t3 (ciclico): LRU y FIFO empatan en 200 fallos y 136 reemplazos" \
  '[ "$(valor "$lru" "Total fallos de página")/$(valor "$fifo" "Total fallos de página")/$(valor "$lru" "Total reemplazos")" = "200/200/136" ]'

echo "== Invariantes en todas las pruebas y politicas"
for t in tests/t*.txt; do
  for p in lru fifo; do
    salida=$($BIN "$t" -p "$p" -v 2>&1)
    verificar "$(basename "$t") $p: hits + fallos == accesos" 'grep -q "suma de control: .* == accesos" <<< "$salida"'
  done
done

echo
if [ "$fallas" -eq 0 ]; then
  echo "Todas las verificaciones pasaron."
else
  echo "$fallas verificacion(es) fallaron."
fi
exit "$fallas"
