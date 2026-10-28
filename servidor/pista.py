#!/usr/bin/env python3
"""Quanto cada radar costuma passar LONGE da trilha que o carro percorreu.

O aparelho decide em tempo real com a proa de agora, e isso tem limite: a
perpendicular de longe é projeção, não medida (RF02.4). O servidor não tem
essa pressa — ele tem o histórico inteiro de viagens e pode medir a coisa
certa, que é a distância do radar até a LINHA realmente percorrida.

    radar sistematicamente a 27 m da minha trilha  -> é de outra pista
    radar sistematicamente a  3 m                  -> é da minha

⚠️ **Não é mapa de vias e não é aprendizado de máquina.** É uma estatística
por ponto: acumular passagens e tirar o ruído. O erro da média cai com √N — 3 m
de ruído de GPS viram ~0,9 m em 10 passadas.

## Duas armadilhas que este módulo existe para não cair

**Ponto-a-amostra mede outra coisa.** A viagem grava a cada 6 s, que a
78 km/h são 130 m entre amostras. A menor distância até uma AMOSTRA
superestima o afastamento em dezenas de metros — mais que a própria separação
entre as pistas. A conta certa é ponto-a-SEGMENTO, interpolando entre amostras
consecutivas.

**Sentido conta separado.** A mesma via dirigida nos dois sentidos põe o radar
da ida a 3 m e o da volta a 40 m. Misturar os dois faz toda mediana convergir
para o meio da pista, e nenhum radar se separa de nenhum. Por isso só entram
segmentos cujo rumo o radar aceitaria — a mesma regra do RF02.3 no firmware.
"""
from __future__ import annotations

import math
import statistics
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import viagem

#: O mesmo raio máximo do RF03.2. Fora dele o firmware nem considera o ponto,
#: e medir afastamento de quem nunca disputou não diz nada.
RAIO_M = 300.0

#: Abaixo desta velocidade o veículo está parado ou a passo, e as amostras
#: pousam todas no mesmo lugar com o ruído por cima. Um radar ao lado de um
#: semáforo onde se para todo dia acumularia distâncias minúsculas que não
#: descrevem pista nenhuma.
V_MINIMA_KMH = 15.0

#: Tolerância do filtro de sentido, igual à do firmware (RF02.3).
TOLERANCIA_RUMO_GRAUS = 30.0

#: Quantas passagens antes de a mediana valer alguma coisa.
#:
#: Três é pouco para estatística e é de propósito: o objetivo não é precisão,
#: é separar 3 m de 27 m. Com menos que isso uma única passada ruim — HDOP
#: alto, viaduto, troca de faixa — viraria verdade sobre o ponto.
PASSAGENS_MINIMAS = 3

#: Lado da célula do índice espacial, em graus. ~550 m: grande o bastante para
#: a vizinhança 3x3 cobrir os 300 m do raio, pequena o bastante para a lista
#: de cada célula ser curta.
CELULA_GRAUS = 0.005

_M_POR_GRAU = 111_320.0


@dataclass(frozen=True)
class Estatistica:
    """O que o histórico diz sobre um radar."""

    lat: float
    lon: float
    limite: int
    passagens: int
    mediana_m: float
    menor_m: float
    maior_m: float


def _metros(lat0: float, lon0: float, lat: float, lon: float) -> tuple[float, float]:
    """Deslocamento (leste, norte) em metros. Equirretangular em torno de lat0."""
    leste = (lon - lon0) * _M_POR_GRAU * math.cos(math.radians(lat0))
    norte = (lat - lat0) * _M_POR_GRAU
    return leste, norte


def diferenca_rumo(a: float, b: float) -> float:
    """Diferença circular entre dois rumos, de 0 a 180 graus."""
    d = abs(a - b) % 360.0
    return 360.0 - d if d > 180.0 else d


def sentido_compativel(radar: viagem.RadarNoMapa, rumo_graus: float) -> bool:
    """O mesmo critério do RF02.3, para a medida bater com o que o aparelho vê."""
    if radar.sentido == 0:                      # omnidirecional
        return True
    d = diferenca_rumo(rumo_graus, float(radar.rumo))
    if radar.sentido == 2:                      # bidirecional: vale o oposto
        d = min(d, 180.0 - d)
    return d <= TOLERANCIA_RUMO_GRAUS


def distancia_ponto_segmento(px: float, py: float, ax: float, ay: float,
                             bx: float, by: float) -> float:
    """Distância de (px,py) ao SEGMENTO ab — não à reta que o contém.

    Projetar na reta infinita atribuiria ao segmento a aproximação de um
    trecho que o carro não percorreu, o que inventa passagens em curva.
    """
    vx, vy = bx - ax, by - ay
    comprimento2 = vx * vx + vy * vy
    if comprimento2 == 0.0:
        return math.hypot(px - ax, py - ay)
    t = ((px - ax) * vx + (py - ay) * vy) / comprimento2
    t = max(0.0, min(1.0, t))
    return math.hypot(px - (ax + t * vx), py - (ay + t * vy))


@dataclass(frozen=True)
class Segmento:
    """Um trecho percorrido entre duas amostras, com o rumo que ele descreve."""

    lat0: float
    lon0: float
    lat1: float
    lon1: float
    rumo: float


def segmentos_de(v: viagem.Viagem) -> list[Segmento]:
    """Os trechos em que o veículo estava de fato andando.

    O rumo sai da GEOMETRIA do segmento, e não da coluna `rumo` do arquivo: o
    segmento é o que se vai medir, e o rumo dele é o que diz se o radar olha
    para este lado. A coluna do receptor descreve o instante de uma ponta.
    """
    saida: list[Segmento] = []
    pontos = v.pontos
    for a, b in zip(pontos, pontos[1:]):
        if a.v_media_kmh < V_MINIMA_KMH or b.v_media_kmh < V_MINIMA_KMH:
            continue
        leste, norte = _metros(a.lat, a.lon, b.lat, b.lon)
        if leste == 0.0 and norte == 0.0:
            continue
        saida.append(Segmento(a.lat, a.lon, b.lat, b.lon,
                              math.degrees(math.atan2(leste, norte)) % 360.0))
    return saida


def _grade(radares: list[viagem.RadarNoMapa]) -> dict[tuple[int, int], list[int]]:
    """Índice espacial simples: célula -> índices de radar.

    Sem ele a conta é 18 mil radares x milhares de segmentos por viagem, que
    em Python leva minutos. Com ele, cada segmento olha só a vizinhança.
    """
    g: dict[tuple[int, int], list[int]] = {}
    for i, r in enumerate(radares):
        chave = (int(r.lat / CELULA_GRAUS), int(r.lon / CELULA_GRAUS))
        g.setdefault(chave, []).append(i)
    return g


def _vizinhos(g: dict[tuple[int, int], list[int]], lat: float,
              lon: float) -> Iterable[int]:
    cy, cx = int(lat / CELULA_GRAUS), int(lon / CELULA_GRAUS)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            yield from g.get((cy + dy, cx + dx), ())


def passagens_da_viagem(v: viagem.Viagem,
                        radares: list[viagem.RadarNoMapa],
                        grade: dict[tuple[int, int], list[int]] | None = None,
                        ) -> dict[int, float]:
    """Para cada radar que esta viagem passou, a menor distância à trilha.

    Uma viagem produz **uma** medida por radar, e não uma por amostra: dez
    amostras perto do mesmo ponto são a mesma passagem, e contá-las dez vezes
    daria peso de dez viagens a uma só.
    """
    if grade is None:
        grade = _grade(radares)
    melhor: dict[int, float] = {}
    for s in segmentos_de(v):
        candidatos = set(_vizinhos(grade, s.lat0, s.lon0))
        candidatos |= set(_vizinhos(grade, s.lat1, s.lon1))
        ax, ay = 0.0, 0.0
        bx, by = _metros(s.lat0, s.lon0, s.lat1, s.lon1)
        for i in candidatos:
            r = radares[i]
            if not sentido_compativel(r, s.rumo):
                continue
            px, py = _metros(s.lat0, s.lon0, r.lat, r.lon)
            d = distancia_ponto_segmento(px, py, ax, ay, bx, by)
            if d <= RAIO_M and (i not in melhor or d < melhor[i]):
                melhor[i] = d
    return melhor


def acumula(viagens: Iterable[viagem.Viagem],
            radares: list[viagem.RadarNoMapa]) -> dict[int, list[float]]:
    """Junta as passagens de várias viagens, por radar."""
    grade = _grade(radares)
    tudo: dict[int, list[float]] = {}
    for v in viagens:
        for i, d in passagens_da_viagem(v, radares, grade).items():
            tudo.setdefault(i, []).append(d)
    return tudo


def estatisticas(acumulado: dict[int, list[float]],
                 radares: list[viagem.RadarNoMapa],
                 passagens_minimas: int = PASSAGENS_MINIMAS,
                 ) -> list[Estatistica]:
    """As medianas, só para os radares com passagens suficientes.

    Mediana e não média: uma passada com HDOP ruim joga a média dezenas de
    metros, e a mediana nem percebe.

    Ordenadas por mediana decrescente — quem está mais longe da trilha é quem
    interessa, porque é quem alerta sem ser da via.
    """
    saida = []
    for i, ds in acumulado.items():
        if len(ds) < passagens_minimas:
            continue
        r = radares[i]
        saida.append(Estatistica(r.lat, r.lon, r.limite, len(ds),
                                 statistics.median(ds), min(ds), max(ds)))
    saida.sort(key=lambda e: e.mediana_m, reverse=True)
    return saida


def relatorio(diretorio, caminho_base, passagens_minimas=PASSAGENS_MINIMAS
              ) -> list[Estatistica]:
    """Lê todas as viagens de um diretório e devolve as estatísticas.

    Arquivo que não é viagem é ignorado em silêncio: `recebidos/` tem também
    o diário e o `infracoes.log`, e abortar por causa deles tornaria o
    relatório refém da faxina do diretório.
    """
    from pathlib import Path

    radares = viagem.le_radares(Path(caminho_base))
    viagens = []
    for arq in sorted(Path(diretorio).glob("*.log")):
        try:
            viagens.append(viagem.le_viagem(arq))
        except (viagem.ErroDeViagem, OSError, ValueError):
            continue
    return estatisticas(acumula(viagens, radares), radares, passagens_minimas)


def main(argv: list[str] | None = None) -> int:
    import argparse

    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("viagens", help="diretório com os .log de viagem")
    p.add_argument("base", help="caminho do radares.bin")
    p.add_argument("--minimo", type=int, default=PASSAGENS_MINIMAS,
                   help="passagens mínimas para o radar entrar")
    a = p.parse_args(argv)

    linhas = relatorio(a.viagens, a.base, a.minimo)
    if not linhas:
        print("nenhum radar com passagens suficientes")
        return 0
    print(f"{'limite':>6} {'passagens':>9} {'mediana':>8} {'menor':>8} "
          f"{'maior':>8}  coordenada")
    for e in linhas:
        print(f"{e.limite:>6} {e.passagens:>9} {e.mediana_m:>7.1f}m "
              f"{e.menor_m:>7.1f}m {e.maior_m:>7.1f}m  "
              f"{e.lat:.5f},{e.lon:.5f}")
    return 0


if __name__ == "__main__":
    import sys as _sys
    raise SystemExit(main(_sys.argv[1:]))
