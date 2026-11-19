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
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from geo import metros  # noqa: E402

AQUI = Path(__file__).resolve().parent
SAIDA = AQUI / "rota_eixao.csv"
# Três espelhos: o principal devolve 504 sob carga com alguma frequência, e
# uma consulta que falha por isso não é motivo para desistir da rota.
ESPELHOS = (
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
    "https://overpass.osm.ch/api/interpreter",
)

#: A caixa do Plano Piloto, (sul, oeste, norte, leste). Vai na consulta E
#: na conferencia: uma via com a mesma `ref` noutro canto do DF entraria na
#: linha de centro e a puxaria para fora do Eixao.
CAIXA = (-15.90, -47.96, -15.70, -47.82)

CONSULTA = f"""
[out:json][timeout:60];
way["ref"="DF-002"]({CAIXA[0]},{CAIXA[1]},{CAIXA[2]},{CAIXA[3]});
out geom;
"""

#: Quanto o Eixao tem de ter, em km, em UM sentido.
#:
#: ⚠️ **E esta conferencia que o CSV versionado nao tinha.** O arquivo era
#: aberto em "w" -- truncado -- antes de a resposta ser olhada: uma Overpass
#: sobrecarregada, que devolve meia via ou nenhuma, destruia a rota boa. E
#: meia rota ainda CARREGA no simulador: o percurso simplesmente fica mais
#: curto, e os radares do resto do Eixao somem sem ninguem notar.
#:
#: A rota medida em 2026-09-24 deu 16,53 km. A folga de 2 km cobre uma
#: remessa de nos um pouco diferente do OSM sem aceitar meia via.
COMPRIMENTO_KM = 16.5
FOLGA_KM = 2.0

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


def elementos_de(pacote: dict) -> list[dict]:
    """Os `elements` de uma resposta da Overpass, ou `ValueError`.

    ⚠️ A Overpass avisa de tempo esgotado com **200 e um `remark`**, e a
    lista de elementos vem vazia. Lido como resposta boa, isso virava
    "nenhuma via DF-002 retornada" -- mensagem que manda procurar o
    problema no OSM, e nao no servidor sobrecarregado.
    """
    remark = pacote.get("remark", "")
    if remark:
        raise ValueError(f"a Overpass respondeu com um aviso: {remark}")
    if "elements" not in pacote:
        raise ValueError("resposta da Overpass sem 'elements'")
    return pacote["elements"]


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
                return elementos_de(json.load(r))
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


def comprimento_km(rota: list[tuple[float, float]]) -> float:
    return sum(metros(rota[i], rota[i + 1])
               for i in range(len(rota) - 1)) / 1000.0


def confere(rota: list[tuple[float, float]]) -> str | None:
    """A rota é o Eixão inteiro, e dentro da caixa? Erro, ou None."""
    if len(rota) < 2:
        return "rota com menos de dois pontos"
    km = comprimento_km(rota)
    if abs(km - COMPRIMENTO_KM) > FOLGA_KM:
        return (f"a rota tem {km:.2f} km, e o Eixao tem ~{COMPRIMENTO_KM} km "
                f"em um sentido (tolerancia de {FOLGA_KM} km). A resposta "
                "veio incompleta, ou a via mudou no OSM -- confira antes de "
                "gravar.")
    sul, oeste, norte, leste = CAIXA
    for lat, lon in rota:
        if not (sul <= lat <= norte and oeste <= lon <= leste):
            return (f"o ponto ({lat:.5f}, {lon:.5f}) esta fora da caixa do "
                    "Plano Piloto")
    return None


def grava(rota: list[tuple[float, float]], nos: int, saida: Path) -> None:
    """Escreve o CSV de forma atômica: temporário ao lado, depois `replace`.

    Entre truncar e terminar de escrever havia uma janela em que o arquivo
    estava pela metade -- e meia rota ainda carrega no simulador. O
    temporário fica no MESMO diretório porque `os.replace` só é atômico
    dentro do mesmo sistema de arquivos.
    """
    quando = datetime.now(timezone.utc).strftime("%Y-%m-%d")
    temporario = saida.with_name(saida.name + ".parcial")
    try:
        with temporario.open("w", encoding="utf-8") as f:
            f.write("# Linha de centro do Eixão (DF-002), sentido sul -> norte.\n")
            f.write(f"# GERADO por gera_rota.py em {quando}. Não edite à mão.\n")
            f.write("#\n")
            f.write("# Origem: OpenStreetMap via Overpass API, ways com ref=DF-002\n")
            f.write(f"# na caixa ({CAIXA[0]},{CAIXA[1]})-({CAIXA[2]},{CAIXA[3]}). "
                    "Dados ODbL.\n")
            f.write(f"# {nos} nós brutos -> {len(rota)} pontos de centro,\n")
            f.write(f"# {comprimento_km(rota):.2f} km de percurso em um sentido.\n")
            f.write("lat,lon\n")
            for lat, lon in rota:
                f.write(f"{lat:.6f},{lon:.6f}\n")
        os.replace(temporario, saida)
    finally:
        temporario.unlink(missing_ok=True)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--de-arquivo", type=Path,
                   help="usa uma resposta da Overpass ja baixada, em JSON")
    args = p.parse_args(argv[1:])

    try:
        if args.de_arquivo:
            print(f"  lendo {args.de_arquivo}")
            elementos = elementos_de(
                json.loads(args.de_arquivo.read_text(encoding="utf-8")))
        else:
            print("  consultando a Overpass API...")
            elementos = baixa()
    except (OSError, ValueError) as e:
        print(f"  erro: {e}")
        return 1
    nos = nos_do_eixao(elementos)
    if not nos:
        print("  erro: nenhuma via DF-002 retornada")
        return 1
    rota = linha_de_centro(nos)

    # Conferir ANTES de tocar no arquivo: o CSV versionado e a unica copia
    # da rota, e o simulador roda sem rede por causa dele.
    erro = confere(rota)
    if erro:
        print(f"  erro: {erro}")
        print(f"  {SAIDA.name} NAO foi tocado.")
        return 1

    grava(rota, len(nos), SAIDA)
    print(f"  {len(nos)} nós -> {len(rota)} pontos, "
          f"{comprimento_km(rota):.2f} km")
    print(f"  escrito: {SAIDA}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
