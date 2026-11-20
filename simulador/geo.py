#!/usr/bin/env python3
"""A geometria que o simulador e o gerador de rota compartilham.

Estava copiada nos dois, com o 111320,0 escrito quatro vezes. Uma cópia
corrigida e a outra não é um simulador que anda por uma rota medida com
outra régua — e a diferença aparece como erro de alerta, não como erro de
conta.
"""
from __future__ import annotations

import math

#: Metros por grau de latitude. A longitude encolhe por cos(lat).
#:
#: O mesmo número do `kMetrosPorGrauLat` do firmware (nucleo/Geo.h), de
#: propósito: o simulador tem de andar pela régua com que o aparelho mede.
M_POR_GRAU = 111_320.0


def deslocamento(a: tuple[float, float],
                 b: tuple[float, float]) -> tuple[float, float]:
    """(leste, norte) de `a` para `b`, em metros."""
    leste = (b[1] - a[1]) * M_POR_GRAU * math.cos(math.radians(a[0]))
    norte = (b[0] - a[0]) * M_POR_GRAU
    return leste, norte


def metros(a: tuple[float, float], b: tuple[float, float]) -> float:
    """Distância entre dois (lat, lon), em metros.

    Equirretangular, a mesma aproximação do firmware e pelo mesmo motivo:
    sobre 16 km o erro contra Haversine é de centímetros.
    """
    leste, norte = deslocamento(a, b)
    return math.hypot(norte, leste)


def rumo_graus(a: tuple[float, float], b: tuple[float, float]) -> float:
    """Rumo de `a` para `b`, em graus do norte, no sentido horário."""
    leste, norte = deslocamento(a, b)
    return math.degrees(math.atan2(leste, norte)) % 360.0
