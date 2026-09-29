#!/usr/bin/env bash
# generar_pruebas.sh - genera las trazas de prueba que no se escriben a mano.
#
#   bash tests/generar_pruebas.sh          regenera t4, t5 y t6 (versionadas en git)
#   bash tests/generar_pruebas.sh estres   genera tests/estres_grande.txt (no versionada)
#
# Los numeros pseudoaleatorios salen de un generador propio (Park-Miller,
# x = x * 16807 mod 2^31-1) y no de rand(): asi la traza es identica con mawk
# (el awk por defecto de Ubuntu/WSL) y con gawk, y los resultados del reporte se
# pueden reproducir en cualquier maquina.
set -euo pipefail
cd "$(dirname "$0")"
PAGINA=4096

# generar_8020 <semilla> <accesos> <paginas> <paginas_calientes> <prob_escritura> <comentario>
# El 80 % de los accesos va a las primeras <paginas_calientes> paginas y el 20 %
# al resto. Todas las direcciones son multiplos de 4 (palabras de 32 bits).
generar_8020() {
  awk -v semilla="$1" -v accesos="$2" -v paginas="$3" -v calientes="$4" \
      -v pesc="$5" -v comentario="$6" -v pagina=$PAGINA '
    function rnd() { x = (x * 16807) % 2147483647; return x / 2147483647 }
    BEGIN {
      x = semilla;
      print comentario;
      print "alloc " paginas * pagina;
      for (i = 0; i < accesos; i++) {
        if (rnd() < 0.8) p = int(rnd() * calientes);
        else             p = calientes + int(rnd() * (paginas - calientes));
        va = p * pagina + int(rnd() * (pagina / 4)) * 4;
        if (rnd() < pesc) print "write " va " " i; else print "read " va;
      }
    }'
}

if [ "${1:-}" = "estres" ]; then
  generar_8020 7 100000 256 51 0.3 \
    "# estres_grande.txt - 100000 accesos, 80/20 sobre 256 paginas (1 MB), 30 % escrituras" \
    > estres_grande.txt
  echo "tests/estres_grande.txt generado"
  exit 0
fi

# t4: localidad 80/20 con presion de memoria. 100 paginas (400 KB) no caben en
# los 64 marcos por defecto; 20 paginas calientes reciben el 80 % de los accesos.
generar_8020 42 5000 100 20 0.3 \
  "# Prueba 4: localidad 80/20 con presion de memoria (100 paginas, 20 calientes, 5000 accesos)" \
  > t4_localidad_8020.txt

# t5: el swap conserva los datos. Se escribe un valor distinto en 80 paginas
# (mas que los 64 marcos) y luego se releen: la pagina k debe devolver k*1000+7.
{
  echo "# Prueba 5: persistencia en swap. 80 paginas escritas y releidas; la pagina k vale k*1000+7"
  echo "alloc $((80 * PAGINA))"
  for k in $(seq 0 79); do echo "write $((k * PAGINA)) $((k * 1000 + 7))"; done
  for k in $(seq 0 79); do echo "read $((k * PAGINA))"; done
} > t5_swap.txt

# t6: caso minimo donde LRU y FIFO se separan. 64 paginas llenan la memoria, la
# pagina 0 (la mas antigua) se usa 5 veces y entra una pagina nueva: LRU conserva
# la pagina 0, FIFO la desaloja y la vuelve a traer.
{
  echo "# Prueba 6: pagina antigua pero caliente. LRU: 65 fallos, FIFO: 66"
  echo "alloc $((65 * PAGINA))"
  for i in $(seq 0 63); do echo "write $((i * PAGINA)) $i"; done
  for _ in 1 2 3 4 5; do echo "read 0"; done
  echo "write $((64 * PAGINA)) 64"
  echo "read 0"
} > t6_discriminante.txt

echo "tests/t4_localidad_8020.txt, t5_swap.txt y t6_discriminante.txt generados"
