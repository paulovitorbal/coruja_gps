#!/usr/bin/env python3
"""Junta os textos das licencas que vao DENTRO do binario, para a release.

BSD-3-Clause, MIT e Apache-2.0 pedem o aviso de copyright e o texto da licenca
junto do binario redistribuido. As bibliotecas nao estao neste repositorio --
chegam pelo Pico SDK e pelo FetchContent --, entao quem baixa so a release nao
as tem. Este script le as copias reais que o build usou e escreve um arquivo
so, que o `libera_fw.sh` anexa a release.

Recusa e NAO grava nada se faltar (ou estiver vazio) qualquer um dos textos:
um pacote incompleto, anexado em silencio, seria pior que nenhum. E apaga o
pacote de uma execucao anterior antes de tentar, para que uma falha nao deixe
o texto velho passando por novo.

Os componentes sao os da tabela de `docs/licencas/README.md`, que sai do mapa
do ligador. O driver SDIO (GPL-3) NAO esta aqui, de proposito: ele foi tirado
do binario -- ver `firmware/cmake/fatfs_sd_sem_sdio.cmake`.

uso: junta_licencas.py --sdk <pico-sdk> --fatfs <no-OS-FatFS> \\
                       --licencas <docs/licencas> --saida <arquivo>
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

# (nome, licenca, base, [arquivos relativos a ela], nota opcional)
COMPONENTES = (
    ("Raspberry Pi Pico SDK", "BSD-3-Clause", "sdk", ["LICENSE.TXT"], ""),
    ("lwIP", "BSD-3-Clause", "sdk", ["lib/lwip/COPYING"], ""),
    ("Mbed TLS", "Apache-2.0 OU GPL-2.0-or-later, a escolha de quem usa",
     "sdk", ["lib/mbedtls/LICENSE"],
     "Este projeto toma a Apache-2.0: a GPL-2.0 nao conviveria com a licenca "
     "nao comercial do projeto."),
    ("TinyUSB", "MIT", "sdk", ["lib/tinyusb/LICENSE"], ""),
    ("cyw43-driver", "propria, NAO e BSD", "sdk",
     ["lib/cyw43-driver/LICENSE", "lib/cyw43-driver/LICENSE.RP"],
     "O aparelho e um RP2350 da Raspberry Pi, entao vale a LICENSE.RP, que "
     "tira a restricao comercial mas vale apenas com silicio da Raspberry "
     "Pi. A LICENSE e a nao comercial."),
    ("no-OS-FatFS-SD-SDIO-SPI-RPi-Pico", "Apache-2.0", "fatfs",
     ["LICENSE"], "Inclui o driver SPI, derivado do Mbed OS (Apache-2.0)."),
    ("FatFs (ChaN)", "propria, de duas clausulas, estilo BSD", "fatfs",
     ["src/ff15/LICENSE.txt"], ""),
    ("JetBrains Mono (mapas de bits derivados)", "SIL OFL 1.1", "licencas",
     ["OFL.txt"], ""),
    ("Twemoji (icones derivados)", "CC-BY 4.0", "licencas",
     ["CC-BY-4.0-twemoji.txt"],
     "Arte do Twemoji, (c) Twitter Inc. e colaboradores."),
)

SEPARADOR = "=" * 78


def monta(bases: dict[str, Path]) -> str:
    """O pacote inteiro, ou levanta `FileNotFoundError` dizendo o que falta."""
    partes = [
        "Licencas dos componentes de terceiros que vao dentro do binario do\n"
        "coruja_gps. Cada texto abaixo e copia do arquivo que o build usou.\n"
        "A licenca do proprio projeto esta em LICENSE.md.\n"
    ]
    faltam: list[str] = []
    for nome, licenca, base, arquivos, nota in COMPONENTES:
        textos = []
        for rel in arquivos:
            caminho = bases[base] / rel
            try:
                texto = caminho.read_text(encoding="utf-8", errors="replace")
            except OSError:
                faltam.append(f"{nome}: {rel} (nao achei {caminho})")
                continue
            if not texto.strip():
                faltam.append(f"{nome}: {rel} (arquivo vazio)")
                continue
            textos.append((rel, texto.rstrip("\n")))
        if not textos:
            continue
        cab = [SEPARADOR, f"{nome} -- {licenca}"]
        if nota:
            cab.append(nota)
        cab.append(SEPARADOR)
        partes.append("\n".join(cab))
        for rel, texto in textos:
            if len(textos) > 1:
                partes.append(f"--- {rel} ---")
            partes.append(texto + "\n")
    if faltam:
        raise FileNotFoundError("\n".join(faltam))
    return "\n".join(partes) + "\n"


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sdk", required=True, type=Path)
    ap.add_argument("--fatfs", required=True, type=Path)
    ap.add_argument("--licencas", required=True, type=Path)
    ap.add_argument("--saida", required=True, type=Path)
    a = ap.parse_args(argv)

    # Antes de tentar: uma falha nao pode deixar o pacote velho no lugar.
    a.saida.unlink(missing_ok=True)
    try:
        pacote = monta({"sdk": a.sdk, "fatfs": a.fatfs,
                        "licencas": a.licencas})
    except FileNotFoundError as e:
        print("ERRO: faltam textos de licenca, e a release nao sai sem eles:\n"
              + str(e), file=sys.stderr)
        return 1
    # Temporario e troca: nunca um arquivo pela metade.
    tmp = a.saida.with_name(a.saida.name + ".tmp")
    tmp.write_text(pacote, encoding="utf-8")
    tmp.replace(a.saida)
    print(f"{a.saida}: {len(COMPONENTES)} componentes, {len(pacote)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
