#!/usr/bin/env bash
# barrido.sh - hit rate de LRU y FIFO en funcion del tamano de la memoria fisica.
# Uso: make barrido  [TRAZA=tests/t4_localidad_8020.txt]
# Imprime una tabla en markdown (para pegar en el reporte) y un grafico de barras
# en texto con los fallos de pagina, escalado para que el maximo mida 50 '#'.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=./simulador
TRAZA=${1:-tests/t4_localidad_8020.txt}
MEMORIAS="256 320 384 448 512 640 768 1024"

campo() {  # campo <salida> <etiqueta>
  grep -m1 "^$2:" <<< "$1" | sed 's/^[^:]*: *//; s/%$//'
}

barra() {  # barra <valor> <maximo>: hasta 50 '#', proporcional
  awk -v v="$1" -v m="$2" 'BEGIN { n = (m > 0) ? int(v * 50 / m + 0.5) : 0;
                                   s = ""; for (i = 0; i < n; i++) s = s "#"; print s }'
}

echo "Traza: $TRAZA"
echo
echo "| Memoria | Marcos | Fallos LRU | Fallos FIFO | Hit rate LRU | Hit rate FIFO | Reemplazos LRU | Reemplazos FIFO |"
echo "|---|---:|---:|---:|---:|---:|---:|---:|"
filas=""
maximo=0
for kb in $MEMORIAS; do
  lru=$($BIN "$TRAZA" -p lru -m "$kb" 2>/dev/null)
  fifo=$($BIN "$TRAZA" -p fifo -m "$kb" 2>/dev/null)
  hl=$(campo "$lru" "Hit rate"); hf=$(campo "$fifo" "Hit rate")
  printf "| %s KB | %s | %s | %s | %s %% | %s %% | %s | %s |\n" "$kb" "$((kb / 4))" \
    "$(campo "$lru" "Total fallos de página")" "$(campo "$fifo" "Total fallos de página")" "$hl" "$hf" \
    "$(campo "$lru" "Total reemplazos")" "$(campo "$fifo" "Total reemplazos")"
  fl=$(campo "$lru" "Total fallos de página"); ff=$(campo "$fifo" "Total fallos de página")
  filas+="$kb $fl $ff"$'\n'
  [ "$fl" -gt "$maximo" ] && maximo=$fl
  [ "$ff" -gt "$maximo" ] && maximo=$ff
done
echo
echo "Fallos de pagina por tamano de memoria (el maximo mide 50 #):"
while read -r kb fl ff; do
  [ -z "$kb" ] && continue
  printf "%5s KB  LRU  %7s  %s\n" "$kb" "$fl" "$(barra "$fl" "$maximo")"
  printf "%5s KB  FIFO %7s  %s\n" "$kb" "$ff" "$(barra "$ff" "$maximo")"
done <<< "$filas"
