#!/usr/bin/env python3
"""Confere que a traseira não tem material onde deveria haver abertura.

⚠️ **Existe porque "o furo está lá" não é a pergunta certa.** Em 08/10/2026 o
furo do GX12 foi acrescentado, o render saiu `Simple: yes`, uma conferência de
malha achou o furo — e o anel de reforço, somado depois do `difference`,
descia por trás do rasgo do cartão e o tapava. Nada disso aparecia nos
números; apareceu quando o autor abriu o modelo e olhou.

O que isto mede é o contrário: **passa alguma coisa sólida pelo vão?**

    python3 verifica_caixa.py caixa.stl
"""
import re
import sys
from pathlib import Path

#: As aberturas da traseira, em (x0, x1, z0, z1). Derivadas das mesmas contas
#: do `.scad` — se elas mudarem lá, mudam aqui, e o teste passa a medir outra
#: coisa. É o preço de verificar a malha e não o modelo.
ABERTURAS = {
    "rasgo do cartao": (57.7, 75.7, 14.2, 19.2),
    "abertura do USB": (33.5, 47.5, 12.4, 21.4),
}
#: Parede traseira: y entre o interno e o externo.
Y_PAREDE = (124.0, 127.0)
#: Margem para não acusar a própria borda da abertura.
MARGEM = 0.8

PADRAO = re.compile(r"vertex\s+([-\d.e+]+)\s+([-\d.e+]+)\s+([-\d.e+]+)")


def vertices(caminho: Path):
    return [tuple(float(x) for x in m.groups())
            for m in PADRAO.finditer(caminho.read_text())]


def invasores(vs, caixa_abertura):
    x0, x1, z0, z1 = caixa_abertura
    return [v for v in vs
            if x0 + MARGEM <= v[0] <= x1 - MARGEM
            and z0 + MARGEM <= v[2] <= z1 - MARGEM
            and Y_PAREDE[0] - 2.0 <= v[1] <= Y_PAREDE[1] + 0.5]


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 64
    vs = vertices(Path(argv[1]))
    print(f"{len(vs)} vertices em {argv[1]}")
    ruim = False
    for nome, caixa in ABERTURAS.items():
        maus = invasores(vs, caixa)
        if maus:
            ruim = True
            ys = sorted({round(v[1], 1) for v in maus})
            print(f"  ❌ {nome}: {len(maus)} vertices DENTRO do vao  (y {ys})")
        else:
            print(f"  ✅ {nome}: vao livre")
    return 1 if ruim else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
