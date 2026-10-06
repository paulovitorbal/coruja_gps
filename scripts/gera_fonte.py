#!/usr/bin/env python3
"""Rasteriza a JetBrains Mono em mapas de bits 1 bpp para a flash.

Regra 8 do coding_rules.md: o valor de referência é calculado FORA do
código. Aqui o "valor" é a forma de cada glifo, e calcular fora significa
rasterizar de uma fonte de verdade em vez de desenhar à mão — que foi o que
a maquete fazia com sete segmentos, e que nunca ia virar a tipografia final.

Por que 1 bpp e não antialiasing: o §4.1 manda o número em branco puro sobre
fundo preto, e o brilho é PWM do backlight, que multiplica a luminância de
tudo pelo mesmo fator. Meio-tom no glifo só existiria para ser apagado no
piso de 5%, gastando 8x a flash para isso.

Saída: cabeçalhos com `constexpr` — nada em RAM, tudo em flash (4 MB), zero
impacto no orçamento de `.bss` do formato_dados.md §1.

Dependência: Pillow, num venv local (`.venv/`, fora do git). A fonte vem de
`~/Library/Fonts`, instalada pelo autor.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

RAIZ = pathlib.Path(__file__).resolve().parent.parent
DESTINO = RAIZ / "firmware" / "src" / "display"

# A SemiBold é a que está instalada, e é a certa para o caso: peso maior
# preserva o traço no piso de brilho, onde a Regular desaparece antes.
FONTE = pathlib.Path.home() / "Library" / "Fonts" / "JetBrainsMono-SemiBold.ttf"

# JetBrains Mono é SIL Open Font License 1.1. A licença exige que o aviso
# de copyright acompanhe a fonte e seus derivados — um mapa de bits
# rasterizado é derivado, então o aviso vai no cabeçalho gerado.
AVISO = """// Gerado por scripts/gera_fonte.py. NÃO EDITAR À MÃO.
//
// Glifos rasterizados da JetBrains Mono SemiBold.
// Copyright 2020 The JetBrains Mono Project Authors
// (https://github.com/JetBrains/JetBrainsMono)
// Licenciada sob a SIL Open Font License 1.1 — ver docs/licencas/OFL.txt.
"""


class Conjunto:
    """Um tamanho de fonte e o que ele precisa desenhar."""

    def __init__(self, nome: str, largura: int, altura: int, glifos: str,
                 razao: float):
        self.nome = nome
        self.largura = largura
        self.altura = altura
        self.glifos = glifos
        # Fração da altura da célula que o tamanho em pontos deve ocupar. A
        # JetBrains Mono tem ascendentes e descendentes folgados; pedir o
        # tamanho igual à altura da célula corta o glifo.
        self.razao = razao


# 56x94 e 12x20 são os do §4.1, e o orçamento de flash de lá conta com eles:
# 11 glifos de 56x94 dão 7,1 KiB, e 95 de 12x20 dão 2,9 KiB.
CONJUNTOS = [
    Conjunto("Numero", 56, 94, "0123456789/", 1.30),
    # O limite, em metade da escala da velocidade. NAO estava no §4.1, e a
    # falta era defeito: "100/120" sao 7 glifos, e a 56 px cada dao 392 px
    # numa tela de 320. O §4.1 fixou 56x94 sem conferir o conteudo maximo,
    # e rodovia de 120 km/h nao e caso extremo.
    #
    # A saida e hierarquia de escala, nao encolher tudo: a velocidade e o
    # que se le de relance e fica grande; o limite e referencia. Com 28 px,
    # o pior caso "120" + "/120" da 168 + 112 = 280 px, com folga.
    # O "%" entra porque o valor do item "brilho" e "80%": sem ele o simbolo
    # sumia e a tela dizia "80". Custa 192 bytes de flash. (2026-10-06)
    Conjunto("NumeroPequeno", 28, 48, "0123456789/%", 1.30),
    Conjunto("Texto", 12, 20, "".join(chr(c) for c in range(32, 127)), 1.35),
    # Faixa superior: relogio, aviso de taxa e barra de brilho. 14x23 em vez
    # de 12x20 porque o autor relatou, dirigindo, que a data e a hora ficavam
    # dificeis de ler. (2026-10-06)
    #
    # 14 e o maior que mantem "TAXA DE GPS REDUZIDA" dentro dos 320 px: a 15
    # daria 300 e a 16, exatos 320 — e a frase passaria a rolar para dizer o
    # que hoje se le de uma vez. A altura de 23 entra nos 26 px da faixa com
    # 2 px de folga em cima e embaixo, sem mexer na geometria do §4.1.
    Conjunto("TextoGrande", 14, 23, "".join(chr(c) for c in range(32, 127)), 1.35),
]


def rasteriza(conj: Conjunto, caminho_fonte: pathlib.Path):
    """Devolve (bytes_por_glifo, [(glifo, bytes)]) em 1 bpp."""
    # Busca o maior tamanho em pontos que ainda caiba na célula. Fixar o
    # ponto por regra de três erra, porque a métrica varia por glifo.
    alvo = int(conj.altura * conj.razao)
    fonte = ImageFont.truetype(str(caminho_fonte), alvo)
    while alvo > 4:
        fonte = ImageFont.truetype(str(caminho_fonte), alvo)
        cabe = True
        for g in conj.glifos:
            caixa = fonte.getbbox(g)
            if caixa is None:
                continue
            if (caixa[2] - caixa[0]) > conj.largura or \
               (caixa[3] - caixa[1]) > conj.altura:
                cabe = False
                break
        if cabe:
            break
        alvo -= 1

    bytes_por_linha = (conj.largura + 7) // 8
    saida = []
    for g in conj.glifos:
        img = Image.new("1", (conj.largura, conj.altura), 0)
        d = ImageDraw.Draw(img)
        caixa = fonte.getbbox(g) or (0, 0, 0, 0)
        # Centraliza horizontalmente e apoia na linha de base comum, para
        # que os dígitos não dancem entre si.
        x = (conj.largura - (caixa[2] - caixa[0])) // 2 - caixa[0]
        y = (conj.altura - (caixa[3] - caixa[1])) // 2 - caixa[1]
        d.text((x, y), g, font=fonte, fill=1)

        linhas = []
        px = img.load()
        for ly in range(conj.altura):
            for bx in range(bytes_por_linha):
                b = 0
                for bit in range(8):
                    lx = bx * 8 + bit
                    if lx < conj.largura and px[lx, ly]:
                        b |= 0x80 >> bit  # MSB primeiro: bit 7 = pixel da esquerda
                linhas.append(b)
        saida.append((g, linhas))
    return alvo, bytes_por_linha, saida


def escreve(conj: Conjunto, pontos: int, bytes_por_linha: int, glifos,
            destino: pathlib.Path) -> None:
    n = len(glifos)
    por_glifo = bytes_por_linha * conj.altura
    linhas = [AVISO, "#pragma once",
              "#include <cstddef>", "#include <cstdint>", "",
              "namespace coruja::fonte {", "",
              f"/// {conj.nome}: {conj.largura}x{conj.altura}, 1 bpp, "
              f"{n} glifos, {pontos} pt.",
              f"/// {n} x {por_glifo} = {n * por_glifo} bytes de flash "
              f"({n * por_glifo / 1024:.1f} KiB).",
              "///",
              "/// Cada linha ocupa "
              f"{bytes_por_linha} byte(s), MSB primeiro: o bit 7 do primeiro",
              "/// byte e o pixel da ESQUERDA. As linhas vem de cima para baixo.",
              f"namespace {conj.nome.lower()} {{",
              f"constexpr int kLargura = {conj.largura};",
              f"constexpr int kAltura = {conj.altura};",
              f"constexpr int kBytesPorLinha = {bytes_por_linha};",
              f"constexpr std::size_t kQuantos = {n};", "",
              "/// Os glifos, na ordem em que aparecem em `kMapa`.",
              f'constexpr char kMapa[] = {escapa(conj.glifos)};', ""]

    linhas.append(f"constexpr std::uint8_t kBitmap[kQuantos]"
                  f"[kBytesPorLinha * kAltura] = {{")
    for g, dados in glifos:
        rot = g if g not in (chr(92), '"', chr(39)) else "?"
        linhas.append(f"    // '{rot}'" if g != ' ' else "    // ' ' (espaco)")
        for i in range(0, len(dados), 12):
            fatia = ", ".join(f"0x{b:02X}" for b in dados[i:i + 12])
            linhas.append(f"    {fatia},")
    linhas.append("};")
    linhas += ["", "/// Indice do glifo em `kBitmap`, ou -1 se nao houver.",
               "constexpr int indice(char c) {",
               "    for (std::size_t i = 0; i < kQuantos; ++i) {",
               "        if (kMapa[i] == c) {",
               "            return static_cast<int>(i);",
               "        }",
               "    }",
               "    return -1;",
               "}", "",
               f"}}  // namespace {conj.nome.lower()}", "",
               "}  // namespace coruja::fonte", ""]
    destino.write_text("\n".join(linhas), encoding="utf-8")
    print(f"  {destino.name}: {conj.largura}x{conj.altura}, {n} glifos, "
          f"{pontos} pt, {n * por_glifo} bytes de flash")


def escapa(s: str) -> str:
    """Escapa a string para um literal C++.

    Usa chr(92) em vez de barra invertida literal de propósito: a primeira
    versão escreveu o escape dentro de camadas de citação e produziu uma
    comparação que nunca casava, deixando `"` virar `\\"` e a barra
    invertida passar crua. O literal gerado não compilava.
    """
    barra = chr(92)
    fora = []
    for c in s:
        if c == barra:
            fora.append(barra + barra)
        elif c == '"':
            fora.append(barra + '"')
        else:
            fora.append(c)
    return '"' + "".join(fora) + '"'


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--fonte", type=pathlib.Path, default=FONTE)
    p.add_argument("--destino", type=pathlib.Path, default=DESTINO)
    args = p.parse_args(argv)

    if not args.fonte.exists():
        print(f"erro: fonte nao encontrada em {args.fonte}", file=sys.stderr)
        print("      instale a JetBrains Mono ou passe --fonte", file=sys.stderr)
        return 1

    print(f"rasterizando {args.fonte.name}")
    total = 0
    for conj in CONJUNTOS:
        pontos, bpl, glifos = rasteriza(conj, args.fonte)
        destino = args.destino / f"Fonte{conj.nome}.h"
        escreve(conj, pontos, bpl, glifos, destino)
        total += len(glifos) * bpl * conj.altura
    print(f"total: {total} bytes ({total / 1024:.1f} KiB) de flash")
    return 0


if __name__ == "__main__":
    sys.exit(main())
