#!/usr/bin/env python3
"""
diferencial.py - prueba diferencial del simulador contra un modelo de referencia.

Genera trazas aleatorias (varios alloc, accesos con localidad, free a mitad de la
traza, direcciones de regiones ya liberadas, y a veces todo en una sola linea) y
compara el simulador en C contra un modelo minimo escrito aqui en Python:

  - total de accesos, fallos y reemplazos, con LRU y con FIFO;
  - el valor devuelto por CADA read (comprueba traduccion, swap y free).

El modelo es independiente del codigo en C: no comparte estructuras, solo la
especificacion (paginas de 4 KB, alloc desde la VA 0 con puntero que solo sube,
palabras de 32 bits, memoria que arranca en cero).

Uso:  python3 tests/diferencial.py [trazas] [semilla]      (make diferencial)
"""
import random
import re
import subprocess
import sys
import tempfile

BIN = "./simulador"
PAGINA = 4096


def modelo(comandos, marcos, politica):
    """Devuelve ((accesos, fallos, reemplazos), [valores leidos])."""
    regiones, siguiente = {}, 0          # base (en paginas) -> numero de paginas
    validas, residentes = set(), {}      # residentes: pagina -> marca de tiempo
    memoria, lecturas = {}, []
    reloj = accesos = fallos = reemplazos = 0
    for linea in comandos:
        t = linea.split()
        if t[0] == "alloc":
            n = (int(t[1]) + PAGINA - 1) // PAGINA
            regiones[siguiente] = n
            validas.update(range(siguiente, siguiente + n))
            siguiente += n
        elif t[0] == "free":
            base = int(t[1]) // PAGINA
            n = regiones.pop(base)
            for p in range(base, base + n):
                validas.discard(p)
                residentes.pop(p, None)
            for va in [va for va in memoria if base <= va // PAGINA < base + n]:
                del memoria[va]
        else:
            va = int(t[1])
            p = va // PAGINA
            if p not in validas:
                continue                 # acceso ilegal: no cuenta
            accesos += 1
            reloj += 1
            if p in residentes:
                if politica == "lru":
                    residentes[p] = reloj
            else:
                fallos += 1
                if len(residentes) >= marcos:
                    victima = min(residentes, key=residentes.get)
                    del residentes[victima]
                    reemplazos += 1
                residentes[p] = reloj
            if t[0] == "write":
                memoria[va] = int(t[2])
            else:
                lecturas.append(memoria.get(va, 0))
    return (accesos, fallos, reemplazos), lecturas


def generar(rng):
    comandos, regiones, siguiente = [], [], 0
    for _ in range(rng.randint(1, 4)):
        n = rng.randint(1, 60)
        comandos.append(f"alloc {n * PAGINA - rng.randint(0, PAGINA - 1)}")
        regiones.append((siguiente, n))
        siguiente += n
    vivas = list(regiones)
    calientes = rng.randint(1, 20)
    for _ in range(rng.randint(50, 2500)):
        if len(vivas) > 1 and rng.random() < 0.003:
            base, _n = vivas.pop(rng.randrange(len(vivas)))
            comandos.append(f"free {base * PAGINA}")
            continue
        base, n = rng.choice(vivas if rng.random() < 0.95 else regiones)
        if rng.random() < 0.7:
            p = base + rng.randrange(min(calientes, n))
        else:
            p = base + rng.randrange(n)
        va = p * PAGINA + rng.randrange(8) * 4
        if rng.random() < 0.4:
            comandos.append(f"write {va} {rng.randrange(2 ** 32)}")
        else:
            comandos.append(f"read {va}")
    return comandos


def main():
    trazas = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    semilla = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    rng = random.Random(semilla)
    corridas = diferencias = 0
    with tempfile.NamedTemporaryFile("w+", suffix=".txt") as archivo:
        for i in range(trazas):
            comandos = generar(rng)
            separador = " " if rng.random() < 0.3 else "\n"
            archivo.seek(0)
            archivo.truncate()
            archivo.write(separador.join(comandos) + "\n")
            archivo.flush()
            kb = rng.choice([256, 260, 300, 384, 512])
            for politica in ("lru", "fifo"):
                esperado, lecturas = modelo(comandos, kb * 1024 // PAGINA, politica)
                salida = subprocess.run([BIN, archivo.name, "-p", politica, "-m", str(kb), "-v"],
                                        capture_output=True, text=True).stdout
                obtenido = tuple(int(re.search(k + r": (\d+)", salida).group(1)) for k in
                                 ("Total de accesos", "Total fallos de página", "Total reemplazos"))
                leidos = [int(v) for v in re.findall(r"\] read  0x[0-9A-F]+ = (\d+)", salida)]
                corridas += 1
                if obtenido != esperado or leidos != lecturas:
                    diferencias += 1
                    print(f"DIFERENCIA traza {i} {politica} {kb} KB: esperado {esperado}, "
                          f"obtenido {obtenido}, lecturas distintas: "
                          f"{sum(a != b for a, b in zip(lecturas, leidos))}")
    print(f"{corridas} corridas, {diferencias} diferencias")
    sys.exit(1 if diferencias else 0)


if __name__ == "__main__":
    main()
