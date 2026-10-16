#!/usr/bin/env python3
"""A string de versão está dentro do `.uf2`? Imprime o que encontrou.

    scripts/versao_no_uf2.py <arquivo.uf2> <tag>

⚠️ **Existe porque `strings` no `.uf2` dá falso negativo.** O formato embala
256 bytes de dado em blocos de 512, com 32 bytes de cabeçalho na frente e
enchimento atrás. Uma string que cruza a fronteira de um bloco fica partida
por bytes de cabeçalho no meio, e o `strings` só enxerga o primeiro pedaço.

Aconteceu com a v0.2.16 em 16/10/2026: o ELF tinha `v0.2.16` e o `.uf2`
mostrava só `v0.2`. A release foi barrada por um firmware correto — e um
portão que reprova o certo acaba sendo desligado, que é pior do que não ter
portão nenhum.

A conferência continua sendo no `.uf2`, que é o artefato que vai para o
aparelho; o que muda é que ele é DESEMBALADO antes de procurar.
"""

import re
import struct
import sys
from pathlib import Path

#: Cabeçalho do bloco UF2: magic, magic2, flags, endereço, tamanho do dado,
#: número do bloco, total de blocos, tipo/famílias — 32 bytes.
CABECALHO = struct.Struct("<8I")
TAM_BLOCO = 512
MAGIC0 = 0x0A324655   # "UF2\n"
MAGIC1 = 0x9E5D5157


def carga(caminho: Path) -> bytes:
    """Concatena o dado dos blocos, sem os cabeçalhos nem o enchimento."""
    bruto = caminho.read_bytes()
    if len(bruto) % TAM_BLOCO != 0:
        raise SystemExit(f"{caminho}: nao e multiplo de {TAM_BLOCO} bytes")
    pedacos = []
    for inicio in range(0, len(bruto), TAM_BLOCO):
        bloco = bruto[inicio:inicio + TAM_BLOCO]
        m0, m1, _flags, _end, tam, _n, _total, _fam = CABECALHO.unpack_from(
            bloco)
        if m0 != MAGIC0 or m1 != MAGIC1:
            raise SystemExit(f"{caminho}: bloco em {inicio} sem o magic do UF2")
        if tam > 476:
            raise SystemExit(f"{caminho}: bloco em {inicio} diz {tam} bytes")
        pedacos.append(bloco[32:32 + tam])
    return b"".join(pedacos)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    dados = carga(Path(argv[1]))
    alvo = argv[2].encode("ascii")
    if dados.find(alvo) >= 0:
        return 0
    # Não achou: diz o que HÁ, para o erro apontar em vez de só negar.
    achados = re.findall(rb"v[0-9][0-9A-Za-z.\-]{2,30}|sem-git", dados)
    vistos: list[str] = []
    for a in achados:
        t = a.decode("ascii", "replace")
        if t not in vistos:
            vistos.append(t)
    print("     o binario tem:", file=sys.stderr)
    for t in vistos[:3]:
        print(f"       {t}", file=sys.stderr)
    if not vistos:
        print("       nenhuma string de versao", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
