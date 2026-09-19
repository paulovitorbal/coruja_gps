#!/usr/bin/env python3
"""Gera os arquivos de configuração do firmware, perguntando ao usuário.

Regra 8 do coding_rules.md. Produz três arquivos, conforme docs/adr/0002:

  coruja.cfg          -> raiz do cartão SD, lido em tempo de execução
  coruja.cfg.exemplo  -> versionado, sem nenhum segredo
  ConfigCalibracao.h  -> compilado no firmware

A senha do Wi-Fi é pedida sem eco e o .cfg é gravado com permissão 600.
Mesmo assim: o cartão é removível e legível por qualquer um, então use uma
rede de convidados ou de IoT para o OTA, nunca a rede principal.
"""

from __future__ import annotations

import argparse
import getpass
import os
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
CFG = "coruja.cfg"
EXEMPLO = "coruja.cfg.exemplo"
HEADER = RAIZ / "firmware" / "src" / "nucleo" / "ConfigCalibracao.h"

# Valores que o firmware assume se o .cfg faltar (ADR 0002).
PADRAO_FUSO = -3           # Brasil não tem horário de verão desde 2019
PADRAO_BRILHO = 40         # por cento
BRILHO_MIN, BRILHO_MAX = 5, 100


def pergunta(rotulo: str, padrao: str = "", obrigatorio: bool = True) -> str:
    sufixo = f" [{padrao}]" if padrao else ""
    while True:
        r = input(f"{rotulo}{sufixo}: ").strip() or padrao
        if r or not obrigatorio:
            return r
        print("  valor obrigatório.", file=sys.stderr)


def pergunta_int(rotulo: str, padrao: int, minimo: int, maximo: int) -> int:
    while True:
        bruto = input(f"{rotulo} [{padrao}]: ").strip()
        if not bruto:
            return padrao
        try:
            v = int(bruto)
        except ValueError:
            print("  informe um número inteiro.", file=sys.stderr)
            continue
        if not minimo <= v <= maximo:
            print(f"  fora da faixa {minimo} a {maximo}.", file=sys.stderr)
            continue
        return v


def pergunta_float(rotulo: str, minimo: float, maximo: float) -> float:
    while True:
        try:
            v = float(input(f"{rotulo}: ").strip().replace(",", "."))
        except ValueError:
            print("  informe um número.", file=sys.stderr)
            continue
        if not minimo <= v <= maximo:
            print(f"  fora da faixa {minimo} a {maximo}.", file=sys.stderr)
            continue
        return v


def coleta_runtime() -> dict:
    print("\n--- configuração de execução (vai para o cartão) ---")
    ssid = pergunta("SSID do Wi-Fi para o OTA")
    senha = getpass.getpass("Senha do Wi-Fi (não aparece na tela): ")
    if not senha:
        print("  aviso: senha vazia; a rede será tratada como aberta.",
              file=sys.stderr)
    fuso = pergunta_int("Fuso horário (UTC+N)", PADRAO_FUSO, -12, 14)
    brilho = pergunta_int("Brilho inicial em %", PADRAO_BRILHO,
                          BRILHO_MIN, BRILHO_MAX)
    return {"ssid": ssid, "senha": senha, "fuso": fuso, "brilho": brilho}


def coleta_calibracao() -> dict:
    print("\n--- calibração do LED RGB (R-05) ---")
    print("Os resistores já foram medidos em 2026-09-19: 330 Ω vermelho,")
    print("470 Ω verde, 150 Ω azul. O que falta são as razões de PWM do")
    print("âmbar e do rosa. Deixe em branco para usar os nominais.")
    if not input("Já tem as medidas? [s/N]: ").strip().lower().startswith("s"):
        return {}
    d = {}
    for canal in ("vermelho", "verde", "azul"):
        d[f"r_{canal}"] = pergunta_float(f"  resistor real do {canal} (ohms)",
                                         1.0, 10000.0)
    d["duty_ambar_r"] = pergunta_float("  duty do vermelho no âmbar (0 a 1)", 0.0, 1.0)
    d["duty_ambar_g"] = pergunta_float("  duty do verde no âmbar (0 a 1)", 0.0, 1.0)
    d["duty_rosa_r"] = pergunta_float("  duty do vermelho no rosa (0 a 1)", 0.0, 1.0)
    d["duty_rosa_b"] = pergunta_float("  duty do azul no rosa (0 a 1)", 0.0, 1.0)
    return d


def corpo_cfg(c: dict, com_segredo: bool) -> str:
    senha = c["senha"] if com_segredo else "troque-me"
    ssid = c["ssid"] if com_segredo else "minha-rede-iot"
    return (
        "# Configuração de execução do Coruja GPS.\n"
        "# Gerado por scripts/gera_config.py. Vai na raiz do cartão microSD.\n"
        "#\n"
        "# O cartão é removível e legível por qualquer um: use uma rede de\n"
        "# convidados ou de IoT, nunca a rede principal da casa.\n"
        f"wifi_ssid={ssid}\n"
        f"wifi_senha={senha}\n"
        f"fuso_utc={c['fuso']}\n"
        f"brilho_inicial={c['brilho']}\n"
    )


def corpo_header(cal: dict) -> str:
    def val(chave: str, nominal: str) -> str:
        return f"{cal[chave]:.4f}F" if chave in cal else nominal

    medido = "medidos na bancada" if cal else "NOMINAIS — ainda não medidos"
    return f"""#pragma once

// Gerado por scripts/gera_config.py. Não edite à mão.
// Valores {medido} (R-05).

namespace coruja::calibracao {{

constexpr bool kMedido = {"true" if cal else "false"};

// Resistores de cada canal, em ohms. Os nominais sao os MEDIDOS na bancada
// em 2026-09-19, sob luz solar direta (R-05).
constexpr float kResistorVermelho = {val("r_vermelho", "330.0F")};
constexpr float kResistorVerde    = {val("r_verde", "470.0F")};
constexpr float kResistorAzul     = {val("r_azul", "150.0F")};

// Razões de PWM que produzem cada cor composta, de 0 a 1.
constexpr float kDutyAmbarVermelho = {val("duty_ambar_r", "1.0F")};
constexpr float kDutyAmbarVerde    = {val("duty_ambar_g", "0.45F")};
constexpr float kDutyRosaVermelho  = {val("duty_rosa_r", "1.0F")};
constexpr float kDutyRosaAzul      = {val("duty_rosa_b", "0.60F")};

}}  // namespace coruja::calibracao
"""


def grava(caminho: Path, conteudo: str, modo: int | None = None) -> None:
    caminho.parent.mkdir(parents=True, exist_ok=True)
    caminho.write_text(conteudo, encoding="utf-8")
    if modo is not None:
        os.chmod(caminho, modo)
    print(f"  escrito: {caminho}")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--destino", type=Path, default=RAIZ,
                   help="onde gravar o coruja.cfg; aponte para o cartão montado")
    p.add_argument("--so-exemplo", action="store_true",
                   help="gera apenas o coruja.cfg.exemplo, sem pedir segredo")
    args = p.parse_args()

    if args.so_exemplo:
        grava(RAIZ / EXEMPLO, corpo_cfg(
            {"ssid": "", "senha": "", "fuso": PADRAO_FUSO,
             "blank": 0, "brilho": PADRAO_BRILHO}, com_segredo=False))
        return 0

    print("Configuração do Coruja GPS")
    runtime = coleta_runtime()
    calibracao = coleta_calibracao()

    print()
    grava(args.destino / CFG, corpo_cfg(runtime, com_segredo=True), modo=0o600)
    grava(RAIZ / EXEMPLO, corpo_cfg(runtime, com_segredo=False))
    grava(HEADER, corpo_header(calibracao))

    if args.destino == RAIZ:
        print(f"\n  atenção: o {CFG} ficou no repositório, não no cartão.")
        print(f"  Copie-o para a raiz do microSD e apague daqui, ou rode de novo")
        print(f"  com --destino /Volumes/NOME_DO_CARTAO")
    if not calibracao:
        print("\n  a calibração saiu com valores NOMINAIS. Rode de novo depois")
        print("  do R-05: sem ela o rosa pode ficar indistinguível do vermelho.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
