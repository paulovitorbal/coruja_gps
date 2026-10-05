"""Coloca o que a planta não decidiu: furos de fixação e peças pequenas.

A planta baixa decide a posição dos cinco módulos, que é onde está a engenharia
— vizinhança de ruído, faceamento, vista de céu. O resto é empacotamento: achar
lugar livre para 17 peças pequenas e 4 furos, sem colidir com nada.

Empacotamento é trabalho de programa. Feito à mão, cada peça é uma chance de
errar em silêncio — e já errei uma vez, em 2026-10-05, aprovando dois furos
contra a zona do módulo em vez da área real do footprint.

## Como funciona

Cada peça tem uma **região de destino**, escolhida por função: a entrada de
12 V fica junto do conversor, os conectores de painel na borda do motorista,
o resistor do GPS perto do GPS. Dentro da região, o colocador varre uma grade
de 1 mm e aceita a primeira posição livre, tentando 0 e 90 graus.

Varredura em grade não dá o arranjo mais bonito. Dá um arranjo **correto**, que
é o que falta agora — o bonito se faz arrastando no editor, e qualquer arrasto
continua válido porque a ligação é por rede, não por posição.
"""
from __future__ import annotations

from kicad_mapa import MAPA
from ocupacao import caixa_na_placa, livre

#: Folga entre áreas de ocupação de peças vizinhas.
#:
#: 2,5 mm e não o mínimo elétrico: com 0,4 as peças cabiam, mas os designadores
#: de serigrafia se atropelavam e o DRC devolvia ~500 violações de texto que
#: afogavam as três elétricas de verdade. Verificação que ninguém lê não
#: verifica nada.
FOLGA = 2.5
#: Passo da varredura.
PASSO = 1.0

#: Regiões livres da placa, em coordenada do KiCad (y cresce para BAIXO, e a
#: traseira — onde saem cartão e cabo — é y pequeno).
#:
#: Obtidas da área real dos módulos já colocados:
#:   conversor  x  2,75..24,60   y 43,17..93,93
#:   Pico       x 26,96..50,04   y  1,20..55,05
#:   microSD    x 51,75..77,65   y  2,89..26,25
#:   GPS        x 48,07..92,33   y 70,75..97,25
#:   antena     x 63,00..88,00   y 41,00..66,00
REGIOES = {
    # Traseira esquerda, acima do conversor: é por onde entra o 12 V, e fica
    # longe da antena.
    "entrada_12v": (3.0, 3.0, 30.0, 54.0),
    # Frente, entre o conversor e o GPS: conectores do chicote de painel e o
    # driver do buzzer, que fica junto do conector dele.
    "painel": (34.0, 58.0, 58.0, 117.0),
    # Entre o leitor e a antena: alimentação do Pico e o resistor do GPS.
    "alimentacao": (62.0, 30.0, 98.0, 48.0),
    # Faixa da direita, de reserva.
    "reserva": (100.0, 3.0, 117.0, 117.0),
}

#: Para onde vai cada peça, e por quê.
DESTINO = {
    # Entrada de 12 V, na ordem em que a corrente passa.
    "J12V": "entrada_12v",     # conector de entrada
    "F1": "entrada_12v",       # fusível, logo depois do conector
    "TVS1": "entrada_12v",     # supressor, em paralelo com a entrada
    "C5": "entrada_12v",       # reservatório de 50 V

    # Saída de 5 V e alimentação do Pico.
    "D1": "alimentacao",       # Schottky de VSYS
    "C1": "alimentacao",       # reservatório de 25 V
    "C2": "alimentacao",       # desacoplamento de VSYS
    "R5": "alimentacao",       # em série GPIO0 -> GPS RX; perto do GPS

    # Painel: conectores do chicote e o driver do buzzer.
    "TFT": "painel",
    "ENC": "painel",
    "LED": "painel",
    "BZ": "painel",
    "Q1": "painel",            # driver do buzzer, junto do conector dele
    "R4": "painel",            # base do Q1
    "R1": "painel",            # limitadores do LED, junto do conector
    "R2": "painel",
    "R3": "painel",
}


def _tenta(caminho: str, regiao: tuple, ocupadas: list) -> tuple | None:
    """Primeira posição livre na região, varrendo em grade e tentando girar."""
    x0, y0, x1, y1 = regiao
    for rot in (0.0, 90.0):
        y = y0
        while y <= y1:
            x = x0
            while x <= x1:
                caixa = caixa_na_placa(caminho, x, y, rot)
                if livre(caixa, ocupadas, regiao, FOLGA):
                    return (x, y, rot, caixa)
                x += PASSO
            y += PASSO
    return None


def coloca_pequenas(ocupadas: list[tuple]) -> tuple[dict, list[str]]:
    """Posição de cada peça pequena. Devolve também o que não coube."""
    pos, sobraram = {}, []
    ocupadas = list(ocupadas)
    # Da maior para a menor: peça grande achando lugar primeiro desperdiça
    # menos espaço que o contrário.
    def area(p):
        c = caixa_na_placa(MAPA[p][2], 0, 0, 0)
        return (c[2] - c[0]) * (c[3] - c[1])

    for peca in sorted(DESTINO, key=area, reverse=True):
        achado = _tenta(MAPA[peca][2], REGIOES[DESTINO[peca]], ocupadas)
        if achado is None:
            sobraram.append(peca)
            continue
        x, y, rot, caixa = achado
        pos[peca] = (x, y, rot)
        ocupadas.append(caixa)
    return pos, sobraram


def coloca_furos(caminho_furo: str, ocupadas: list[tuple],
                 placa: tuple, recuo: float = 10.0) -> tuple[list, list[str]]:
    """Um furo por quadrante, o mais perto que der da quina livre."""
    L, P = placa
    cantos = [(recuo, recuo), (L - recuo, recuo),
              (recuo, P - recuo), (L - recuo, P - recuo)]
    furos, problemas = [], []
    ocupadas = list(ocupadas)
    for i, (ax, ay) in enumerate(cantos, start=1):
        melhor = None
        # Espiral grosseira a partir da quina pretendida.
        for raio in range(0, 60, 2):
            for dx in range(-raio, raio + 1, 2):
                for dy in (-raio, raio) if raio else (0,):
                    for px, py in ((ax + dx, ay + dy), (ax + dy, ay + dx)):
                        if not (5 <= px <= L - 5 and 5 <= py <= P - 5):
                            continue
                        caixa = caixa_na_placa(caminho_furo, px, py)
                        if livre(caixa, ocupadas, (0, 0, L, P), FOLGA):
                            melhor = (px, py, caixa)
                            break
                    if melhor:
                        break
                if melhor:
                    break
            if melhor:
                break
        if melhor is None:
            problemas.append(f"furo {i}: sem lugar livre perto de ({ax}, {ay})")
            continue
        furos.append((melhor[0], melhor[1]))
        ocupadas.append(melhor[2])
    return furos, problemas
