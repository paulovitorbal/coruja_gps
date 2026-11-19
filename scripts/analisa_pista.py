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
from datetime import datetime, timezone
from pathlib import Path
from typing import NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import formato_radares as F  # noqa: E402

#: O mesmo raio do RF03.2. Fora dele o firmware nem considera o ponto.
RAIO_M = 300.0

#: Metros por grau de latitude. A longitude encolhe por cos(lat).
M_POR_GRAU = 111_320.0


#: A primeira linha de todo log de viagem. Ver formato_logs.md, §1.
ASSINATURA = "# coruja_gps viagem "

#: As versoes que este leitor conhece. As colunas sao lidas por indice, e uma
#: versao nova pode mudar o SIGNIFICADO de uma delas -- foi o que a v2 fez
#: com o segundo do carimbo. Ler uma desconhecida como se fosse a v3 daria
#: numeros com cara de medida e sentido de outra coisa.
VERSOES = ("v1", "v2", "v3")


class Viagem(NamedTuple):
    versao: str
    pontos: list[dict]
    #: Linhas de dado que nao se deixaram ler. Contadas, e nao descartadas
    #: em silencio: um arquivo meio corrompido mediria so a metade boa, e
    #: quem le o resultado acharia que mediu a viagem inteira.
    invalidas: int


def interpreta_linha(linha: str) -> dict | None:
    """Uma amostra, ou None se a linha nao for uma amostra legivel."""
    c = linha.split(";")
    if len(c) < 5:
        return None
    try:
        quando = datetime.strptime(c[0], "%Y-%m-%dT%H:%M:%SZ")
        lat, lon, v = float(c[1]), float(c[2]), float(c[3])
    except ValueError:
        return None
    # `float()` aceita "nan" e "inf", e um nan passa por toda comparação
    # como falso: a amostra sumiria da conta sem erro nenhum.
    if not (-90.0 <= lat <= 90.0 and -180.0 <= lon <= 180.0 and
            0.0 <= v < math.inf):
        return None
    try:
        return {
            "utc": c[0],
            # Em segundos: e o tempo, e nao a posicao na lista, que diz se
            # duas amostras sao vizinhas. Ver `rumo_suavizado`.
            "t": quando.replace(tzinfo=timezone.utc).timestamp(),
            "lat": lat,
            "lon": lon,
            "v_kmh": v,
            "radar_m": float(c[5]) if len(c) > 5 and c[5] else None,
            "radar_kmh": int(c[6]) if len(c) > 6 and c[6] else None,
            "perto_m": float(c[7]) if len(c) > 7 and c[7] else None,
            "perto_kmh": int(c[8]) if len(c) > 8 and c[8] else None,
            "rumo": float(c[9]) if len(c) > 9 and c[9] else None,
            "zona": int(c[10]) if len(c) > 10 and c[10] else None,
            "n_radares": int(c[11]) if len(c) > 11 and c[11] else None,
        }
    except ValueError:
        return None


def interpreta_viagem(texto: str) -> Viagem:
    """As amostras de um log de viagem, com a versao conferida.

    Levanta `ValueError` se o texto nao for log de viagem ou se a versao for
    desconhecida.
    """
    linhas = texto.splitlines()
    primeira = next((l.strip() for l in linhas if l.strip()), "")
    if not primeira.startswith(ASSINATURA):
        raise ValueError(f"nao e log de viagem: a primeira linha devia ser "
                         f"'{ASSINATURA}vN', e e {primeira[:40]!r}")
    versao = primeira[len(ASSINATURA):].strip()
    if versao not in VERSOES:
        raise ValueError(f"log de viagem {versao!r} desconhecido; este "
                         f"leitor conhece {', '.join(VERSOES)}")
    pontos = []
    invalidas = 0
    for linha in linhas:
        linha = linha.strip()
        if not linha or linha.startswith("#") or linha.startswith("utc;"):
            continue
        p = interpreta_linha(linha)
        if p is None:
            invalidas += 1
        else:
            pontos.append(p)
    return Viagem(versao, pontos, invalidas)


def le_viagem(caminho: Path) -> Viagem:
    return interpreta_viagem(Path(caminho).read_text(encoding="utf-8"))


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


#: Até quantos segundos de cada lado entram na estimativa de rumo.
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
#:
#: **Em segundos, e não em amostras.** Cinco amostras de cada lado eram 30 s
#: enquanto o log não tinha buraco; depois de um túnel ou de o aparelho ficar
#: desligado, as cinco anteriores estavam minutos atrás, noutra rua, e o rumo
#: saía da reta entre as duas. 30 s são as mesmas cinco fatias de 6 s da v2/v3.
JANELA_RUMO_S = 30.0

#: Abaixo disto a amostra fica fora da perpendicular.
#:
#: Parado, o receptor não sabe a direção: a RMC vem sem rumo, e a base longa
#: passa a medir o ruído do GPS em volta de um ponto só. A perpendicular
#: calculada sobre esse rumo é número aleatório com cara de medida. 10 km/h é
#: escolha, não medição: em 30 s a essa velocidade o carro anda 83 m, base
#: que já aguenta os metros de ruído do GPS.
VELOCIDADE_MIN_KMH = 10.0

#: Maior intervalo entre duas amostras que ainda se liga por uma reta.
#:
#: Na v2/v3 as fatias são de 6 s: 15 s tolera UMA fatia perdida, com folga
#: para o arredondamento do segundo. Acima disso houve túnel ou aparelho
#: desligado, e a reta seria um caminho que ninguém percorreu. A v1, com uma
#: amostra por minuto, nunca liga: a 80 km/h a corda teria 1,3 km, e o quanto
#: ela se afasta de uma curva comum passa da separação que se quer medir.
LACUNA_MAX_S = 15.0


def rumo_suavizado(viagem: Viagem, i: int) -> float | None:
    """O rumo em `i`, de uma base longa em vez de duas amostras.

    A base vai da amostra mais antiga à mais nova dentro de `JANELA_RUMO_S`
    de cada lado. Devolve `None` quando falta amostra de um dos lados —
    pontas do arquivo e bordas de lacuna —, onde inventar rumo produziria
    perpendicular sem significado.
    """
    pontos = viagem.pontos
    # O rumo REGISTRADO pelo receptor vence sempre: ele vem por Doppler e
    # acerta ~1°, contra os vários graus de qualquer estimativa feita sobre
    # posições ruidosas. Só se ele faltar -- arquivo v1/v2, ou veículo parado
    # -- é que se recorre à base longa.
    registrado = pontos[i].get("rumo")
    if registrado is not None:
        return registrado
    t = pontos[i]["t"]
    a = i
    while a > 0 and t - pontos[a - 1]["t"] <= JANELA_RUMO_S:
        a -= 1
    b = i
    while b + 1 < len(pontos) and pontos[b + 1]["t"] - t <= JANELA_RUMO_S:
        b += 1
    if a == i or b == i:
        return None
    return rumo_entre(pontos[a]["lat"], pontos[a]["lon"],
                      pontos[b]["lat"], pontos[b]["lon"])


def proximos(base, lat: float, lon: float, raio_m: float):
    """Os radares a menos de `raio_m`: (índice na base, radar, distância,
    leste, norte).

    O índice identifica o radar. Dois registros iguais na base continuam
    sendo dois radares, e a estatística é por radar.
    """
    grau = raio_m / M_POR_GRAU
    saida = []
    for k, r in enumerate(base):
        if abs(r.lat - lat) > grau:
            continue
        leste, norte = metros_relativos(lat, lon, r.lat, r.lon)
        d = math.hypot(leste, norte)
        if d <= raio_m:
            saida.append((k, r, d, leste, norte))
    return saida


def analisa(viagem: Viagem, base) -> list[dict]:
    """Para cada amostra em movimento, a perpendicular de cada radar na
    janela."""
    achados = []
    for i, p in enumerate(viagem.pontos):
        if p["v_kmh"] < VELOCIDADE_MIN_KMH:
            continue
        rumo = rumo_suavizado(viagem, i)
        if rumo is None:
            continue
        for k, r, d, leste, norte in proximos(base, p["lat"], p["lon"],
                                              RAIO_M):
            ao_longo, perp = ao_longo_e_perpendicular(leste, norte, rumo)
            achados.append({
                "radar": k,
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


def perpendicular_por_radar(achados: list[dict]) -> dict[int, list[float]]:
    """Por limite, a mediana de |perpendicular| de cada radar à frente.

    **Um valor por radar, e não por amostra.** Um radar visto por vinte
    amostras pesava vinte vezes mais que um visto por uma — e quantas
    amostras o veem depende da velocidade, não da pista. A mediana do limite
    dizia mais sobre onde o carro andou devagar que sobre onde ele andou.
    """
    por_radar: dict[int, list[float]] = {}
    limites: dict[int, int] = {}
    for a in achados:
        if not a["a_frente"]:
            continue
        por_radar.setdefault(a["radar"], []).append(abs(a["perp_m"]))
        limites[a["radar"]] = a["limite"]
    por_limite: dict[int, list[float]] = {}
    for k, perps in por_radar.items():
        por_limite.setdefault(limites[k], []).append(statistics.median(perps))
    return por_limite


def distancia_ao_segmento(px: float, py: float, bx: float,
                          by: float) -> float:
    """Distância de (px, py) ao segmento que vai da origem a (bx, by)."""
    comprimento2 = bx * bx + by * by
    if comprimento2 == 0.0:
        return math.hypot(px, py)
    u = max(0.0, min(1.0, (px * bx + py * by) / comprimento2))
    return math.hypot(px - u * bx, py - u * by)


def trechos(pontos: list[dict]):
    """Os pares (a, b) de amostras ligadas por uma reta.

    Uma amostra que não se liga à seguinte -- última do arquivo, ou borda de
    lacuna -- sai como (a, a), segmento de comprimento zero: ela também foi
    posição do carro.
    """
    for j, a in enumerate(pontos):
        seguinte = pontos[j + 1] if j + 1 < len(pontos) else None
        if seguinte is not None and seguinte["t"] - a["t"] <= LACUNA_MAX_S:
            yield a, seguinte
        else:
            yield a, a


def aproximacao_minima(viagem: Viagem, base) -> dict:
    """A menor distância que a trajetória chegou de cada radar.

    **É esta a medida limpa da separação entre as pistas**, e ela não usa rumo
    nenhum: a aproximação mínima a um ponto da SUA pista é o erro do GPS; a um
    ponto da pista ao lado é a separação entre elas. Nenhuma trigonometria,
    nenhum ruído de rumo.

    **Até a trajetória, e não até a amostra.** A 80 km/h as amostras ficam a
    133 m; um radar em cima da pista, entre duas delas, ficava a 67 m da mais
    próxima — número de pista ao lado. A trajetória é a sequência de retas
    entre amostras vizinhas no tempo (ver `LACUNA_MAX_S`).

    Serve para MEDIR, e não para o firmware: o aparelho precisa decidir antes
    de passar pelo radar, e aí a aproximação mínima ainda não aconteceu. Lá a
    conta tem de ser a perpendicular. Mas é este número que diz se existe
    limiar possível.
    """
    minimos: dict[int, float] = {}
    for a, b in trechos(viagem.pontos):
        bx, by = metros_relativos(a["lat"], a["lon"], b["lat"], b["lon"])
        alcance = RAIO_M + math.hypot(bx, by)
        for k, _r, _d, leste, norte in proximos(base, a["lat"], a["lon"],
                                                alcance):
            d = distancia_ao_segmento(leste, norte, bx, by)
            if d <= RAIO_M and d < minimos.get(k, math.inf):
                minimos[k] = d
    por_limite: dict[int, list[float]] = {}
    for k, d in minimos.items():
        por_limite.setdefault(base[k].limite, []).append(d)
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
    por_limite = perpendicular_por_radar(achados)
    if not por_limite:
        return "nenhum radar a frente na janela de 300 m em toda a viagem"
    linhas = ["", "=== perpendicular por limite (so radares A FRENTE) ===",
              "Cada radar entra UMA vez, com a mediana das suas amostras.",
              f"{'limite':>7} {'radares':>8} {'|perp| mediana':>16} "
              f"{'p10':>8} {'p90':>8} {'min dist':>9}"]
    for limite in sorted(por_limite):
        perps = sorted(por_limite[limite])
        menor = min(a["dist_m"] for a in achados
                    if a["a_frente"] and a["limite"] == limite)
        linhas.append(
            f"{limite:>7} {len(perps):>8} {statistics.median(perps):>16.1f} "
            f"{perps[len(perps)//10]:>8.1f} {perps[9*len(perps)//10]:>8.1f} "
            f"{menor:>9.1f}")
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
    try:
        viagem = le_viagem(Path(argv[1]))
        base = F.le(argv[2])
    except (OSError, ValueError) as e:
        print(f"erro: {e}", file=sys.stderr)
        return 1
    print(f"log de viagem      : {viagem.versao}")
    print(f"amostras na viagem : {len(viagem.pontos)}")
    print(f"linhas invalidas   : {viagem.invalidas}")
    print(f"radares na base    : {len(base)}")
    if viagem.invalidas:
        print("⚠️  as linhas invalidas ficaram de fora: o resultado mede so o "
              "resto do arquivo")
    if viagem.versao != "v3":
        print(f"⚠️  log {viagem.versao}: sem rumo registrado, o rumo sai da "
              "base longa, que erra mais")
    print(resumo_aproximacao(aproximacao_minima(viagem, base)))
    print(resumo(analisa(viagem, base)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
