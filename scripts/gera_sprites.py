#!/usr/bin/env python3
"""Converte os sprites de ícone em RGB565 para a flash.

Os dois ícones do §4.1 são 🏎 (`U+1F3CE`) e 🚦 (`U+1F6A6`), e os arquivos
de origem são do **Twemoji** (CC-BY 4.0), versionados em `ativos/sprites/`.

Por que 40×40 e RGB565 sem máscara: é o que o §4.1 orçou — 2 sprites de
40×40 a 2 bytes por pixel dão exatamente 6.400 bytes. Sem canal alfa, o
transparente do PNG é composto **sobre o fundo** na conversão, e é por isso
que o ícone só pode ser desenhado sobre `paleta::kFundo`. Guardar máscara
custaria 200 bytes por sprite e só serviria se o fundo variasse, o que o
§4.1 diz que não acontece: fundo preto é permanente, e não por estética —
com o brilho a 5% à noite, quanto menor a área acesa, menor o ofuscamento.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

from PIL import Image

RAIZ = pathlib.Path(__file__).resolve().parent.parent
ORIGEM = RAIZ / "ativos" / "sprites"
DESTINO = RAIZ / "firmware" / "src" / "display"
LADO = 40

SPRITES = [("Radar", "radar_1f3ce.png", "U+1F3CE 🏎"),
           ("Semaforo", "semaforo_1f6a6.png", "U+1F6A6 🚦")]

AVISO = """// Gerado por scripts/gera_sprites.py. NÃO EDITAR À MÃO.
//
// Arte do Twemoji, © Twitter Inc. e colaboradores.
// Licenciada sob CC-BY 4.0 — ver docs/licencas/CC-BY-4.0-twemoji.txt.
"""


def para565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def converte(caminho: pathlib.Path, fundo=(0, 0, 0)):
    img = Image.open(caminho).convert("RGBA")
    # LANCZOS porque a redução de 72 para 40 não é inteira: vizinho mais
    # próximo deixaria serrilhado grosseiro num ícone que já é pequeno.
    img = img.resize((LADO, LADO), Image.LANCZOS)
    base = Image.new("RGBA", (LADO, LADO), (*fundo, 255))
    base.alpha_composite(img)
    px = base.convert("RGB").load()
    return [para565(*px[x, y]) for y in range(LADO) for x in range(LADO)]


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--origem", type=pathlib.Path, default=ORIGEM)
    p.add_argument("--destino", type=pathlib.Path, default=DESTINO)
    args = p.parse_args(argv)

    linhas = [AVISO, "#pragma once", "#include <cstdint>", "",
              "namespace coruja::sprite {", "",
              f"constexpr int kLado = {LADO};",
              "",
              "/// RGB565, linha por linha de cima para baixo. O transparente",
              "/// do PNG ja vem composto sobre PRETO: desenhar sobre outro",
              "/// fundo deixaria uma moldura escura em volta do icone.", ""]
    total = 0
    for nome, arquivo, descricao in SPRITES:
        caminho = args.origem / arquivo
        if not caminho.exists():
            print(f"erro: {caminho} nao encontrado", file=sys.stderr)
            return 1
        dados = converte(caminho)
        total += len(dados) * 2
        linhas.append(f"/// {descricao}, de {arquivo}.")
        linhas.append(f"constexpr std::uint16_t k{nome}[kLado * kLado] = {{")
        for i in range(0, len(dados), 10):
            fatia = ", ".join(f"0x{v:04X}" for v in dados[i:i + 10])
            linhas.append(f"    {fatia},")
        linhas.append("};")
        linhas.append("")
        print(f"  {nome}: {LADO}x{LADO}, {len(dados) * 2} bytes")
    linhas += ["}  // namespace coruja::sprite", ""]
    destino = args.destino / "Sprites.h"
    destino.write_text("\n".join(linhas), encoding="utf-8")
    print(f"  {destino.name}: {total} bytes ({total / 1024:.1f} KiB) de flash")
    return 0


if __name__ == "__main__":
    sys.exit(main())
