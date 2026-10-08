#!/usr/bin/env python3
"""Mede se dá para separar as pistas de uma via rápida pela geometria.

O problema, de 07/10/2026: dirigindo pelo Eixão na pista PRINCIPAL a 80 km/h,
o aparelho só mostrava radares de 60 km/h. A pista lateral corre a poucos
metros, seus radares caem na mesma janela de 300 m, e o alvo vence por
GRAVIDADE (RF03.4) — 80 contra limite 60 é Perigo, 80 contra 80 é Conforme.

A ideia do conserto é descartar o radar da outra pista pela **distância
perpendicular** entre ele e a linha que o carro de fato percorreu. Mas o
limiar depende de dois números que ninguém mediu ainda:

  1. a separação real entre as pistas naquele trecho;
  2. o ruído do GPS ali.

Se a separação for muito maior que o ruído, o filtro é trivial e seguro. Se
forem da mesma ordem, não dá para separar e a resposta tem de ser outra.
**Este script produz esses dois números a partir de uma viagem real.**

    python3 scripts/analisa_pista.py <viagem.log> <radares.bin>

⚠️ Ele NÃO decide nada e NÃO muda o firmware. Ele mede, para a decisão ser
tomada com dado em vez de palpite.
"""
from __future__ import annotations

import math
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import formato_radares as F  # noqa: E402

#: O mesmo raio do RF03.2. Fora dele o firmware nem considera o ponto.
RAIO_M = 300.0

#: Metros por grau de latitude. A longitude encolhe por cos(lat).
M_POR_GRAU = 111_320.0


def le_viagem(caminho: Path) -> list[dict]:
    """As amostras de um log de viagem v3, com as colunas de radar."""
    pontos = []
    for linha in Path(caminho).read_text(encoding="utf-8").splitlines():
        linha = linha.strip()
        if not linha or linha.startswith("#") or linha.startswith("utc;"):
            continue
        c = linha.split(";")
        if len(c) < 5:
            continue
        try:
            p = {
                "utc": c[0],
                "lat": float(c[1]),
                "lon": float(c[2]),
                "v_kmh": float(c[3]),
                "radar_m": float(c[5]) if len(c) > 5 and c[5] else None,
                "radar_kmh": int(c[6]) if len(c) > 6 and c[6] else None,
                "perto_m": float(c[7]) if len(c) > 7 and c[7] else None,
                "perto_kmh": int(c[8]) if len(c) > 8 and c[8] else None,
            }
        except ValueError:
            continue
        pontos.append(p)
    return pontos


def metros_relativos(lat0: float, lon0: float, lat: float, lon: float):
    """Deslocamento (leste, norte) em metros, de (lat0,lon0) para (lat,lon).

    Equirretangular em torno de `lat0`. Sobre algumas centenas de metros o
    erro é de centímetros — muito abaixo do ruído do próprio GPS, que é o que
    se está tentando medir.
    """
    leste = (lon - lon0) * M_POR_GRAU * math.cos(math.radians(lat0))
    norte = (lat - lat0) * M_POR_GRAU
    return leste, norte


def ao_longo_e_perpendicular(leste: float, norte: float, rumo_graus: float):
    """Decompõe um deslocamento nas direções do rumo e perpendicular a ele.

    `rumo_graus` é medido do norte, no sentido horário — a convenção do GPS.
    O versor de avanço é (sen θ, cos θ); o perpendicular à direita é
    (cos θ, −sen θ).

    Devolve (ao_longo, perpendicular). Perpendicular positiva = à direita.
    """
    t = math.radians(rumo_graus)
    ao_longo = leste * math.sin(t) + norte * math.cos(t)
    perpendicular = leste * math.cos(t) - norte * math.sin(t)
    return ao_longo, perpendicular


def rumo_entre(lat0: float, lon0: float, lat1: float, lon1: float) -> float:
    """Rumo de um ponto ao seguinte, em graus do norte."""
    leste, norte = metros_relativos(lat0, lon0, lat1, lon1)
    return math.degrees(math.atan2(leste, norte)) % 360.0


#: Quantas amostras de cada lado entram na estimativa de rumo.
#:
#: ⚠️ **Duas amostras consecutivas NÃO servem.** A 10 pontos por minuto e
#: 80 km/h, elas ficam a ~133 m uma da outra; mas a 30 km/h ficam a 50 m, e com
#: 3 m de ruído de GPS o rumo sai com vários graus de erro. A 200 m de
#: distância, 18° de erro viram 60 m de erro na perpendicular — mais que a
#: própria separação entre as pistas, que é o que se quer medir.
#:
#: Medido num ensaio sintético em 2026-10-08: com rumo de duas amostras, a
#: mediana da perpendicular deu 20 m para a pista onde o carro estava, contra
#: os ~0 m esperados. A base longa resolve.
JANELA_RUMO = 5


def rumo_suavizado(viagem: list[dict], i: int) -> float | None:
    """O rumo em `i`, de uma base longa em vez de duas amostras.

    Usa do `i-JANELA_RUMO` ao `i+JANELA_RUMO`. Devolve `None` nas pontas, onde
    não há base — inventar rumo ali produziria perpendicular sem significado.
    """
    a = i - JANELA_RUMO
    b = i + JANELA_RUMO
    if a < 0 or b >= len(viagem):
        return None
    return rumo_entre(viagem[a]["lat"], viagem[a]["lon"],
                      viagem[b]["lat"], viagem[b]["lon"])


def proximos(base, lat: float, lon: float, raio_m: float):
    """Os radares a menos de `raio_m`, com distância e deslocamento."""
    grau = raio_m / M_POR_GRAU
    saida = []
    for r in base:
        if abs(r.lat - lat) > grau:
            continue
        leste, norte = metros_relativos(lat, lon, r.lat, r.lon)
        d = math.hypot(leste, norte)
        if d <= raio_m:
            saida.append((r, d, leste, norte))
    return saida


def analisa(viagem: list[dict], base) -> list[dict]:
    """Para cada amostra, a perpendicular de cada radar na janela."""
    achados = []
    for i, p in enumerate(viagem):
        rumo = rumo_suavizado(viagem, i)
        if rumo is None:
            continue
        for r, d, leste, norte in proximos(base, p["lat"], p["lon"], RAIO_M):
            ao_longo, perp = ao_longo_e_perpendicular(leste, norte, rumo)
            achados.append({
                "utc": p["utc"],
                "v_kmh": p["v_kmh"],
                "limite": r.limite,
                "dist_m": d,
                "ao_longo_m": ao_longo,
                "perp_m": perp,
                "a_frente": ao_longo > 0,
                "radar_kmh_logado": p["radar_kmh"],
                "perto_kmh_logado": p["perto_kmh"],
            })
    return achados


def aproximacao_minima(viagem: list[dict], base) -> dict:
    """A menor distância que a trajetória chegou de cada radar.

    **É esta a medida limpa da separação entre as pistas**, e ela não usa rumo
    nenhum: a aproximação mínima a um ponto da SUA pista é o erro do GPS; a um
    ponto da pista ao lado é a separação entre elas. Nenhuma trigonometria,
    nenhum ruído de rumo.

    Serve para MEDIR, e não para o firmware: o aparelho precisa decidir antes
    de passar pelo radar, e aí a aproximação mínima ainda não aconteceu. Lá a
    conta tem de ser a perpendicular. Mas é este número que diz se existe
    limiar possível.
    """
    minimos: dict[int, float] = {}
    limites: dict[int, int] = {}
    for p in viagem:
        for r, d, _le, _no in proximos(base, p["lat"], p["lon"], RAIO_M):
            chave = id(r)
            if chave not in minimos or d < minimos[chave]:
                minimos[chave] = d
                limites[chave] = r.limite
    por_limite: dict[int, list[float]] = {}
    for chave, d in minimos.items():
        por_limite.setdefault(limites[chave], []).append(d)
    return por_limite


def resumo_aproximacao(por_limite: dict) -> str:
    if not por_limite:
        return "nenhum radar a menos de 300 m da trajetoria"
    linhas = ["", "=== APROXIMACAO MINIMA por limite (a medida limpa) ===",
              f"{'limite':>7} {'radares':>8} {'menor':>8} {'mediana':>9} "
              f"{'maior':>8}"]
    for limite in sorted(por_limite):
        v = sorted(por_limite[limite])
        linhas.append(f"{limite:>7} {len(v):>8} {v[0]:>8.1f} "
                      f"{statistics.median(v):>9.1f} {v[-1]:>8.1f}")
    ordenados = sorted(por_limite)
    if len(ordenados) >= 2:
        menores = [min(por_limite[l]) for l in ordenados]
        linhas += ["",
                   "Se um limite tem aproximacao minima de poucos metros e o",
                   "outro de dezenas, a trajetoria passou POR CIMA do primeiro",
                   "e AO LADO do segundo -- as pistas sao separaveis, e o",
                   f"limiar fica entre {min(menores):.0f} m e {max(menores):.0f} m."]
    return "\n".join(linhas)


def resumo(achados: list[dict]) -> str:
    if not achados:
        return "nenhum radar na janela de 300 m em toda a viagem"
    linhas = ["", "=== perpendicular por limite (so radares A FRENTE) ===",
              f"{'limite':>7} {'n':>5} {'|perp| mediana':>16} "
              f"{'p10':>8} {'p90':>8} {'min dist':>9}"]
    por_limite: dict[int, list[dict]] = {}
    for a in achados:
        if a["a_frente"]:
            por_limite.setdefault(a["limite"], []).append(a)
    for limite in sorted(por_limite):
        g = por_limite[limite]
        perps = sorted(abs(x["perp_m"]) for x in g)
        linhas.append(
            f"{limite:>7} {len(g):>5} {statistics.median(perps):>16.1f} "
            f"{perps[len(perps)//10]:>8.1f} {perps[9*len(perps)//10]:>8.1f} "
            f"{min(x['dist_m'] for x in g):>9.1f}")
    linhas += ["",
               "Leitura: se a mediana de |perp| de um limite ficar perto de",
               "zero e a do outro bem acima, as pistas SAO separaveis e o",
               "limiar fica entre as duas. Se as faixas p10-p90 se tocarem,",
               "nao da para separar com este GPS neste trecho."]
    return "\n".join(linhas)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__)
        return 64
    viagem = le_viagem(Path(argv[1]))
    base = F.le(argv[2])
    print(f"amostras na viagem : {len(viagem)}")
    print(f"radares na base    : {len(base)}")
    tem_v3 = any(p["radar_kmh"] is not None or p["perto_kmh"] is not None
                 for p in viagem)
    if not tem_v3:
        print("⚠️  nenhuma coluna de radar preenchida: o arquivo e v1/v2, ou "
              "a viagem nao passou perto de radar nenhum")
    print(resumo_aproximacao(aproximacao_minima(viagem, base)))
    achados = analisa(viagem, base)
    print(resumo(achados))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
