#!/usr/bin/env python3
"""Generador de frases por cadenas de Markov, sobre un corpus con registros.

Uso normal — expandir el corpus del grupo y sacar SOLO frases nuevas para
que una persona las revise:

    python3 markov.py corpus.txt --orden 2 --nuevas 50 > propuestas.txt

Uso de diagnóstico — medir si el corpus da para recombinar:

    python3 markov.py corpus.txt --medir

Formato del corpus: una frase por línea, `registro: frase`. Las líneas que
empiezan por # se ignoran.

La medida que decide todo es la tensión entre dos columnas: subir el orden
da frases más gramaticales pero converge en repetir el corpus literalmente;
bajarlo da novedad de verdad pero rompe la concordancia del español.
"""
import argparse, collections, random, sys

INICIO, FIN = "\x02", "\x03"


def cargar(ruta):
    registros = collections.defaultdict(list)
    for linea in open(ruta, encoding="utf-8"):
        linea = linea.strip()
        if not linea or linea.startswith("#") or ":" not in linea:
            continue
        reg, frase = linea.split(":", 1)
        registros[reg.strip()].append(frase.strip())
    return registros


def construir(frases, orden):
    """estado (orden palabras) -> lista de palabras que pueden seguir."""
    tabla = collections.defaultdict(list)
    for frase in frases:
        toks = [INICIO] * orden + frase.split() + [FIN]
        for i in range(len(toks) - orden):
            tabla[tuple(toks[i:i + orden])].append(toks[i + orden])
    return tabla


def generar(tabla, orden, limite=40):
    estado, salida = (INICIO,) * orden, []
    for _ in range(limite):
        siguientes = tabla.get(estado)
        if not siguientes:
            break
        palabra = random.choice(siguientes)   # frecuencia = peso, por repetición
        if palabra == FIN:
            break
        salida.append(palabra)
        estado = (estado + (palabra,))[1:]
    return " ".join(salida)


def medir(registros, intentos=200):
    print(f"{'orden':>6} {'registro':>12} {'copias literales':>18} {'nuevas':>8}")
    for orden in (1, 2, 3):
        for reg, frases in registros.items():
            conocidas = set(frases)
            tabla = construir(frases, orden)
            outs = [g for g in (generar(tabla, orden) for _ in range(intentos)) if g]
            copias = sum(1 for o in outs if o in conocidas)
            nuevas = {o for o in outs if o not in conocidas}
            print(f"{orden:>6} {reg:>12} {copias / len(outs) * 100:>17.0f}% {len(nuevas):>8}")
        print()


def proponer(registros, orden, cuantas, intentos_max=20000):
    """Solo frases que NO están en el corpus. Salida para revisión humana."""
    for reg, frases in registros.items():
        conocidas = set(frases)
        tabla = construir(frases, orden)
        vistas, intentos = [], 0
        while len(vistas) < cuantas and intentos < intentos_max:
            g = generar(tabla, orden)
            intentos += 1
            if g and g not in conocidas and g not in vistas:
                vistas.append(g)
        print(f"# {reg} — {len(vistas)} propuestas (orden {orden})")
        for g in vistas:
            print(f"{reg}: {g}")
        if len(vistas) < cuantas:
            print(f"#   (el corpus de '{reg}' no da para más; "
                  f"escribe más frases o baja el orden)")
        print()


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("corpus")
    p.add_argument("--orden", type=int, default=2, help="palabras de contexto (1-3)")
    p.add_argument("--nuevas", type=int, default=20, help="propuestas por registro")
    p.add_argument("--medir", action="store_true", help="tabla copias/novedad")
    p.add_argument("--semilla", type=int, default=7)
    a = p.parse_args()

    random.seed(a.semilla)
    regs = cargar(a.corpus)
    if not regs:
        sys.exit(f"{a.corpus}: no encontré frases con formato 'registro: frase'")
    total = sum(len(v) for v in regs.values())
    palabras = sum(len(f.split()) for v in regs.values() for f in v)
    print(f"# corpus: {total} frases, {len(regs)} registros, {palabras} palabras\n")
    medir(regs) if a.medir else proponer(regs, a.orden, a.nuevas)
