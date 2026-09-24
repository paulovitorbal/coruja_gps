#!/usr/bin/env python3
"""Gera a rota do Eixão a partir do OpenStreetMap.

Existe separado do simulador por uma razão: a rota precisa de **procedência**.
Coordenadas inventadas à mão passariam por reais e ninguém saberia — e um
simulador que anda por uma via imaginária valida o firmware contra ficção.

Aqui a geometria vem do OSM, pela Overpass API, consultando `ref=DF-002` na
caixa do Plano Piloto. O resultado é gravado em `rota_eixao.csv`, que é
versionado para o simulador funcionar sem rede.

Rode de novo só se quiser atualizar a geometria:

    python3 gera_rota.py
"""
from __future__ import annotations

import argparse
import json
import math
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

AQUI = Path(__file__).resolve().parent
SAIDA = AQUI / "rota_eixao.csv"
# Três espelhos: o principal devolve 504 sob carga com alguma frequência, e
# uma consulta que falha por isso não é motivo para desistir da rota.
ESPELHOS = (
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
    "https://overpass.osm.ch/api/interpreter",
)

CONSULTA = """
[out:json][timeout:60];
way["ref"="DF-002"](-15.90,-47.96,-15.70,-47.82);
out geom;
"""

# A ponte do Bragueto aparece na consulta por compartilhar a referência, mas
# fica além do Eixão e puxaria a rota para fora da via.
EXCLUIR = ("Bragueto",)

# Faixas de latitude para a linha de centro, e janela da média móvel.
#
# As duas pistas do Eixão ficam a ~40 m uma da outra. Sem suavização, a média
# por faixa alterna entre elas conforme quais nós caem em cada uma, e a linha
# ziguezagueia: medido, 10,2° de virada média num eixo praticamente reto. Com
# estes valores cai para 4,9°, e o que sobra é a curva real da via.
FAIXAS = 100
JANELA = 2


def baixa() -> list[dict]:
    corpo = urllib.parse.urlencode({"data": CONSULTA}).encode()
    ultimo = ""
    for espelho in ESPELHOS:
        try:
            pedido = urllib.request.Request(
                espelho, data=corpo,
                headers={"User-Agent": "coruja_gps-simulador/1.0"})
            with urllib.request.urlopen(pedido, timeout=90) as r:
                print(f"  respondeu: {espelho}")
                return json.load(r)["elements"]
        except (urllib.error.URLError, OSError, ValueError) as e:
            ultimo = f"{espelho}: {e}"
            print(f"  falhou, tentando o proximo -- {ultimo}")
            time.sleep(2)
    raise SystemExit(f"  erro: nenhum espelho respondeu. Ultimo: {ultimo}")


def nos_do_eixao(elementos: list[dict]) -> list[tuple[float, float]]:
    nos: list[tuple[float, float]] = []
    for e in elementos:
        if e.get("type") != "way" or not e.get("geometry"):
            continue
        nome = e.get("tags", {}).get("name", "")
        if any(x in nome for x in EXCLUIR):
            continue
        nos += [(p["lat"], p["lon"]) for p in e["geometry"]]
    return nos


def linha_de_centro(nos: list[tuple[float, float]]) -> list[tuple[float, float]]:
    lo = min(n[0] for n in nos)
    hi = max(n[0] for n in nos)
    baldes: list[list[float]] = [[] for _ in range(FAIXAS)]
    for lat, lon in nos:
        i = min(FAIXAS - 1, int((lat - lo) / (hi - lo) * FAIXAS))
        baldes[i].append(lon)

    bruto = [(lo + (i + 0.5) * (hi - lo) / FAIXAS, sum(b) / len(b))
             for i, b in enumerate(baldes) if b]

    suave = []
    for i, (lat, _) in enumerate(bruto):
        a = max(0, i - JANELA)
        z = min(len(bruto), i + JANELA + 1)
        suave.append((lat, sum(p[1] for p in bruto[a:z]) / (z - a)))
    return suave


def metros(a: tuple[float, float], b: tuple[float, float]) -> float:
    return math.hypot((b[0] - a[0]) * 111320.0,
                      (b[1] - a[1]) * 111320.0 * math.cos(math.radians(a[0])))


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--de-arquivo", type=Path,
                   help="usa uma resposta da Overpass ja baixada, em JSON")
    args = p.parse_args(argv[1:])

    if args.de_arquivo:
        print(f"  lendo {args.de_arquivo}")
        elementos = json.loads(args.de_arquivo.read_text(encoding="utf-8"))["elements"]
    else:
        print("  consultando a Overpass API...")
        elementos = baixa()
    nos = nos_do_eixao(elementos)
    if not nos:
        print("  erro: nenhuma via DF-002 retornada")
        return 1
    rota = linha_de_centro(nos)
    comprimento = sum(metros(rota[i], rota[i + 1]) for i in range(len(rota) - 1))

    quando = datetime.now(timezone.utc).strftime("%Y-%m-%d")
    with SAIDA.open("w", encoding="utf-8") as f:
        f.write("# Linha de centro do Eixão (DF-002), sentido sul -> norte.\n")
        f.write(f"# GERADO por gera_rota.py em {quando}. Não edite à mão.\n")
        f.write("#\n")
        f.write("# Origem: OpenStreetMap via Overpass API, ways com ref=DF-002\n")
        f.write("# na caixa (-15.90,-47.96)-(-15.70,-47.82). Dados ODbL.\n")
        f.write(f"# {len(nos)} nós brutos -> {len(rota)} pontos de centro,\n")
        f.write(f"# {comprimento/1000:.2f} km de percurso em um sentido.\n")
        f.write("lat,lon\n")
        for lat, lon in rota:
            f.write(f"{lat:.6f},{lon:.6f}\n")

    print(f"  {len(nos)} nós -> {len(rota)} pontos, {comprimento/1000:.2f} km")
    print(f"  escrito: {SAIDA}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
