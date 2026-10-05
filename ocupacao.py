"""Quanto cada footprint realmente ocupa na placa, e o que colide com o quê.

Existe por causa de um erro de 2026-10-05: a conferência da `planta.py` testava
os furos de fixação contra a **zona do módulo** — um retângulo que eu havia
digitado — e aprovou dois furos que o DRC depois reprovou.

O footprint ocupa mais que o módulo. As ilhas de jumper do conversor avançam
2,7 mm para cada lado do contorno, e o furo caiu em cima delas.

A lição é a de sempre neste projeto: **verificar a coisa, não a abstração da
coisa**. Aqui a coisa é a área de ocupação (`F.CrtYd`) do footprint de verdade,
transformada pela posição e rotação com que ele foi colocado.
"""
from __future__ import annotations

import math
import pathlib

from expressao_s import carrega, filho, filhos

FOOTPRINTS = pathlib.Path(
    "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints")
PROPRIA = pathlib.Path(__file__).parent / "kicad"

#: Camadas que delimitam a ocupação, em ordem de preferência. A área de
#: ocupação é a resposta certa; o contorno de fabricação serve de reserva para
#: footprint que não a tenha.
CAMADAS = ("F.CrtYd", "F.Fab", "F.SilkS")


def _arquivo(caminho_lib: str) -> pathlib.Path:
    lib, _, nome = caminho_lib.partition(":")
    raiz = PROPRIA if lib == "coruja" else FOOTPRINTS
    return raiz / f"{lib}.pretty" / f"{nome}.kicad_mod"


def _pontos_da_camada(no: list, camada: str) -> list[tuple[float, float]]:
    pts: list[tuple[float, float]] = []
    for tag in ("fp_line", "fp_rect", "fp_poly", "fp_circle", "fp_arc"):
        for g in filhos(no, tag):
            cam = filho(g, "layer")
            if not cam or cam[1] != camada:
                continue

            # ⚠️ Círculo precisa virar CAIXA, não dois pontos. Amostrar o
            # centro e um ponto da borda perde a extensão nas outras direções,
            # e o footprint mede menor do que é.
            #
            # Foi o que aconteceu em 2026-10-05 com os eletrolíticos radiais e
            # o TVS: o colocador os julgou pequenos, encaixou vizinho em cima,
            # e quem acusou foi o DRC. A verificação de colisão estava certa; o
            # dado que ela recebia é que estava errado.
            if tag == "fp_circle":
                c, e = filho(g, "center"), filho(g, "end")
                if c and e:
                    cx, cy = float(c[1]), float(c[2])
                    r = ((float(e[1]) - cx) ** 2 + (float(e[2]) - cy) ** 2) ** 0.5
                    pts += [(cx - r, cy - r), (cx + r, cy + r)]
                continue

            for chave in ("start", "end", "center", "mid"):
                f = filho(g, chave)
                if f:
                    pts.append((float(f[1]), float(f[2])))
            poly = filho(g, "pts")
            if poly:
                pts += [(float(p[1]), float(p[2])) for p in poly[1:]]
    return pts


def caixa_local(caminho_lib: str) -> tuple[float, float, float, float]:
    """Retângulo que envolve o footprint, em coordenada dele.

    Inclui as ilhas, porque é delas que vem a surpresa: o contorno do módulo
    não é o que ocupa espaço na placa.
    """
    arq = _arquivo(caminho_lib)
    if not arq.exists():
        raise SystemExit(f"footprint inexistente: {caminho_lib}")
    no = carrega(arq.read_text())

    pts: list[tuple[float, float]] = []
    for camada in CAMADAS:
        pts = _pontos_da_camada(no, camada)
        if pts:
            break

    for p in filhos(no, "pad"):
        at = filho(p, "at")
        tam = filho(p, "size")
        px, py = float(at[1]), float(at[2])
        w = float(tam[1]) / 2 if tam else 0.0
        h = float(tam[2]) / 2 if tam else 0.0
        pts += [(px - w, py - h), (px + w, py + h)]

    if not pts:
        raise SystemExit(f"footprint sem geometria: {caminho_lib}")
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return (min(xs), min(ys), max(xs), max(ys))


def gira(px: float, py: float, rot: float) -> tuple[float, float]:
    """Ponto local para deslocamento na placa, na convenção do KiCad.

    O ângulo é anti-horário na tela, e a tela tem Y para baixo. Conferido
    contra o que o DRC reportou: local (24,13; -8,89) num footprint a 90 graus
    apareceu deslocado de (-8,89; -24,13).
    """
    a = math.radians(rot)
    return (px * math.cos(a) + py * math.sin(a),
            -px * math.sin(a) + py * math.cos(a))


def caixa_na_placa(caminho_lib: str, x: float, y: float,
                   rot: float = 0.0) -> tuple[float, float, float, float]:
    """Retângulo ocupado na placa, já posicionado e girado."""
    x0, y0, x1, y1 = caixa_local(caminho_lib)
    cantos = [gira(cx, cy, rot)
              for cx, cy in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
    xs = [x + c[0] for c in cantos]
    ys = [y + c[1] for c in cantos]
    return (min(xs), min(ys), max(xs), max(ys))


def colidem(a: tuple, b: tuple, folga: float = 0.0) -> bool:
    return (a[0] - folga < b[2] and b[0] - folga < a[2]
            and a[1] - folga < b[3] and b[1] - folga < a[3])


def livre(caixa: tuple, ocupadas: list[tuple], limites: tuple,
          folga: float) -> bool:
    """A caixa cabe dentro dos limites e não encosta em nada ocupado."""
    if not (limites[0] <= caixa[0] and caixa[2] <= limites[2]
            and limites[1] <= caixa[1] and caixa[3] <= limites[3]):
        return False
    return not any(colidem(caixa, o, folga) for o in ocupadas)
