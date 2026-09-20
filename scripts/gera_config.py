#!/usr/bin/env python3
"""Gera o coruja.cfg do firmware, perguntando ao usuário.

Regra 8 do coding_rules.md. Produz dois arquivos:

  coruja.cfg          -> raiz do cartão microSD, lido em tempo de execução
  coruja.cfg.exemplo  -> versionado, sem nenhum segredo

CONTÉM APENAS O QUE VARIA POR INSTALAÇÃO: as redes Wi-Fi e as duas URLs.
Brilho, fuso, tolerâncias de velocidade, raio de alerta e calibração do LED
ficam no código, por decisão do autor em 2026-09-20 — se mudar o valor muda o
comportamento de segurança, é especificação e não configuração. Ver
docs/adr/0002.

A senha é pedida sem eco e o .cfg é gravado com permissão 600. Ainda assim:
o cartão é removível e legível por qualquer um, então use uma rede de
convidados ou de IoT, nunca a principal.
"""

from __future__ import annotations

import argparse
import getpass
import os
import sys
from pathlib import Path
from typing import NamedTuple

RAIZ = Path(__file__).resolve().parent.parent
CFG = "coruja.cfg"
EXEMPLO = "coruja.cfg.exemplo"

# Espelham as constantes de firmware/src/nucleo/Configuracao.h. Divergir aqui
# produz um arquivo que o firmware recusa em silêncio, então o teste compara
# os dois.
MAX_REDES = 5
MAX_SSID = 32
MAX_SENHA = 63
MAX_URL = 160


class Rede(NamedTuple):
    ssid: str
    senha: str


def valida_rede(ssid: str, senha: str) -> str | None:
    """Devolve a mensagem de erro, ou None se estiver válido."""
    if len(ssid) > MAX_SSID:
        return f"SSID tem {len(ssid)} caracteres; o máximo é {MAX_SSID}"
    if senha and len(senha) > MAX_SENHA:
        return f"senha tem {len(senha)} caracteres; o máximo é {MAX_SENHA}"
    if senha and len(senha) < 8:
        return "senha de WPA2 precisa de pelo menos 8 caracteres"
    return None


def valida_url(url: str) -> str | None:
    if len(url) > MAX_URL:
        return f"URL tem {len(url)} caracteres; o máximo é {MAX_URL}"
    if not url.startswith(("http://", "https://")):
        return "a URL precisa começar com http:// ou https://"
    if url.startswith("http://"):
        return ("o RF05.2 exige HTTPS: em HTTP, quem estiver na mesma rede "
                "pode substituir a base de radares")
    return None


def pergunta(rotulo: str, obrigatorio: bool = True) -> str:
    while True:
        r = input(f"{rotulo}: ").strip()
        if r or not obrigatorio:
            return r
        print("  valor obrigatório.", file=sys.stderr)


def coleta_redes() -> list[Rede]:
    print(f"\n--- redes Wi-Fi (até {MAX_REDES}) ---")
    print("A ordem é a PRIORIDADE: ao clicar no encoder o aparelho varre as")
    print("redes e conecta na primeira desta lista que estiver visível.")
    print("Deixe a SSID em branco para terminar.\n")

    redes: list[Rede] = []
    while len(redes) < MAX_REDES:
        ssid = pergunta(f"SSID da rede {len(redes) + 1}", obrigatorio=False)
        if not ssid:
            break
        senha = getpass.getpass("  senha (vazia = rede aberta, não aparece): ")
        erro = valida_rede(ssid, senha)
        if erro:
            print(f"  {erro}", file=sys.stderr)
            continue
        redes.append(Rede(ssid, senha))
        if not senha:
            print("  aviso: rede sem senha; qualquer um na área pode ver o "
                  "tráfego.", file=sys.stderr)
    return redes


def coleta_urls() -> tuple[str, str]:
    print("\n--- origem da base de radares ---")
    print("Duas URLs: uma devolve a versão disponível — uma linha de texto")
    print("qualquer, que o aparelho compara com a que já tem — e a outra")
    print("entrega o radares.bin.\n")

    def pede(rotulo: str) -> str:
        while True:
            url = pergunta(rotulo)
            erro = valida_url(url)
            if erro:
                print(f"  {erro}", file=sys.stderr)
                continue
            return url

    return (pede("URL da versão   (ex.: https://exemplo/radares.versao)"),
            pede("URL do download (ex.: https://exemplo/radares.bin)"))


def corpo_cfg(redes: list[Rede], url_versao: str, url_base: str,
              com_segredo: bool) -> str:
    linhas = [
        "# Configuração do Coruja GPS. Vai na raiz do cartão microSD.",
        "# Gerado por scripts/gera_config.py.",
        "#",
        "# O cartão é removível e legível por qualquer um: use uma rede de",
        "# convidados ou de IoT, nunca a principal da casa.",
        "#",
        "# Só entra aqui o que varia por instalação. Brilho, fuso, tolerâncias",
        "# e calibração do LED ficam no código (docs/adr/0002).",
        "",
        f"# Redes em ordem de PRIORIDADE, até {MAX_REDES}. Ao clicar no encoder o",
        "# aparelho varre e conecta na primeira desta lista que estiver visível.",
    ]
    if not redes:
        redes = [Rede("minha-rede-iot", "troque-me"),
                 Rede("celular", "troque-me")]
        com_segredo = False
    for i, r in enumerate(redes, start=1):
        linhas.append(f"wifi_ssid_{i}={r.ssid if com_segredo else 'troque-me'}")
        linhas.append(f"wifi_senha_{i}={r.senha if com_segredo else 'troque-me'}")
    linhas += [
        "",
        "# Devolve a versão disponível: uma linha de texto qualquer, comparada",
        "# como texto com a que o aparelho guardou.",
        f"url_versao={url_versao if com_segredo else 'https://exemplo/radares.versao'}",
        "# Entrega o radares.bin. O RF05.2 exige HTTPS.",
        f"url_base={url_base if com_segredo else 'https://exemplo/radares.bin'}",
        "",
    ]
    return "\n".join(linhas)


def grava(caminho: Path, conteudo: str, modo: int | None = None) -> None:
    caminho.parent.mkdir(parents=True, exist_ok=True)
    caminho.write_text(conteudo, encoding="utf-8")
    if modo is not None:
        os.chmod(caminho, modo)
    print(f"  escrito: {caminho}")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--destino", type=Path, default=RAIZ,
                   help="onde gravar o coruja.cfg; aponte para o cartão montado")
    p.add_argument("--so-exemplo", action="store_true",
                   help="gera apenas o coruja.cfg.exemplo, sem pedir segredo")
    args = p.parse_args(argv)

    if args.so_exemplo:
        grava(RAIZ / EXEMPLO, corpo_cfg([], "", "", com_segredo=False))
        return 0

    print("Configuração do Coruja GPS")
    redes = coleta_redes()
    if not redes:
        print("\nerro: nenhuma rede informada; sem rede não há atualização OTA.",
              file=sys.stderr)
        return 1
    url_versao, url_base = coleta_urls()

    print()
    grava(args.destino / CFG,
          corpo_cfg(redes, url_versao, url_base, com_segredo=True), modo=0o600)
    grava(RAIZ / EXEMPLO, corpo_cfg([], "", "", com_segredo=False))

    if args.destino.resolve() == RAIZ:
        print(f"\n  atenção: o {CFG} ficou no repositório, não no cartão.")
        print("  Copie-o para a raiz do microSD e apague daqui, ou rode de novo")
        print("  com --destino /Volumes/NOME_DO_CARTAO")
    return 0


if __name__ == "__main__":
    sys.exit(main())
