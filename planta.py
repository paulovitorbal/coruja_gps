"""Planta baixa: face frontal e arranjo da PCB, em coordenada.

Separado do desenho (`gera_planta.py`) de propósito: aqui ficam as **posições
decididas**, que são argumento de projeto; lá fica só o traço.

## O que é medida e o que é decisão

Tudo que tem dimensão vem de `medidas.py`, tirado da peça. O que está aqui são
as **posições**, e posição é escolha — nenhuma delas foi medida, todas podem
mudar. Por isso cada uma carrega o porquê.

## As restrições que amarram o arranjo

1. **Cartão e USB faceados na traseira** (decisão de 2026-10-05). Isso obriga o
   leitor microSD e o Pico a encostarem na borda de trás, com a boca do cartão
   e o conector USB apontando para fora. Deixa de ser preferência de
   roteamento: é requisito de montagem.

2. **Antena longe do conversor.** A fonte de ruído é o chaveamento e o indutor
   do LM2596, não os capacitores. Conversor na extremidade esquerda e antena na
   direita dão a maior separação que a placa permite.

3. **Nada sob nem sobre a antena.** Plano de terra sólido embaixo, sem trilha
   atravessando; e o patch enxerga o céu pela tampa, então nenhuma peça pode
   ficar por cima.

4. **O display pendura para dentro.** São 70 x 46,7 mm atrás da face frontal,
   invadindo ~6 mm. A borda frontal da PCB tem de ficar livre de peça alta.
"""
from __future__ import annotations

from medidas import ANTENA, BUZZER, CONVERSOR, DISPLAY, ENCODER, GPS, LEITOR_SD, MONTAGEM

# ----------------------------------------------------------- face frontal --

#: Parede impressa e folga até o módulo mais externo.
MARGEM_FACE = 5.0
#: Respiro entre as três colunas da face.
GAP_FACE = 3.0

_BORDA_BAIXO = MONTAGEM["borda_abaixo_do_display"]

#: Altura da face: borda de baixo + display + margem de cima. O display manda,
#: porque 46,7 + 10 supera os 34,6 do buzzer em pé.
FACE_A = _BORDA_BAIXO + DISPLAY["largura"] + MARGEM_FACE

#: Da esquerda para a direita: encoder, display, buzzer. Ordem escolhida pelo
#: autor — o encoder fica do lado do motorista.
_colunas = [
    ("encoder", ENCODER["comprimento"], ENCODER["largura"]),
    ("display", DISPLAY["comprimento"], DISPLAY["largura"]),
    ("buzzer", BUZZER["diametro"], BUZZER["orelha_a_orelha"]),
]
FACE_L = (2 * MARGEM_FACE + sum(c[1] for c in _colunas)
          + GAP_FACE * (len(_colunas) - 1))

#: Centro vertical do display, a que encoder e buzzer se alinham.
_CY = _BORDA_BAIXO + DISPLAY["largura"] / 2


def _coluna_x() -> dict[str, float]:
    x, saida = MARGEM_FACE, {}
    for nome, larg, _ in _colunas:
        saida[nome] = x
        x += larg + GAP_FACE
    return saida


_X = _coluna_x()

#: Caixas da face: (nome, x, y, largura, altura, papel)
FACE_CAIXAS = [
    ("encoder", _X["encoder"], _CY - ENCODER["largura"] / 2,
     ENCODER["comprimento"], ENCODER["largura"], "corpo"),
    ("display", _X["display"], _BORDA_BAIXO,
     DISPLAY["comprimento"], DISPLAY["largura"], "corpo"),
    # O recorte real da face é a área visível, centrada no módulo.
    ("recorte do vidro",
     _X["display"] + (DISPLAY["comprimento"] - DISPLAY["visivel"][0]) / 2,
     _BORDA_BAIXO + (DISPLAY["largura"] - DISPLAY["visivel"][1]) / 2,
     DISPLAY["visivel"][0], DISPLAY["visivel"][1], "recorte"),
]

#: Círculos da face: (x, y, diâmetro, papel)
FACE_CIRCULOS = [
    # Eixo do encoder, no centro do corpo. ⚠️ O diâmetro da bucha NÃO foi
    # medido; 7,0 é o valor típico do KY-040 e está aqui só para o desenho.
    (_X["encoder"] + ENCODER["comprimento"] / 2, _CY, 7.0, "a medir"),
    # Buzzer: corpo e as duas orelhas, orelhas na vertical.
    (_X["buzzer"] + BUZZER["diametro"] / 2, _CY, BUZZER["diametro"], "corpo"),
    (_X["buzzer"] + BUZZER["diametro"] / 2, _CY + BUZZER["vao_entre_furos"] / 2,
     BUZZER["furo_diam"], "furo"),
    (_X["buzzer"] + BUZZER["diametro"] / 2, _CY - BUZZER["vao_entre_furos"] / 2,
     BUZZER["furo_diam"], "furo"),
]

# -------------------------------------------------------------------- PCB --

#: 120 x 120 mm, decidido em 2026-10-05 depois de medir o aperto.
#:
#: Em 100 x 100 a ocupação era de 64 % e a folga máxima entre peças pequenas
#: ficava em 1,6 mm — abaixo disso o porta-fusível de 26 mm não cabia. Em
#: 120 x 120 a ocupação cai para 44 %, que é folga para rotear.
#:
#: Sai do degrau de preço de 100 x 100, e essa foi a troca aceita: numa placa
#: que vai ser fabricada uma vez, espaço vale mais que o degrau.
#:
#: ⚠️ 120 x 58 chegou a ser proposto e foi descartado: apesar de mais largo, é
#: 30 % MENOS área, dava 96 % de ocupação, e a coluna GPS+antena (54 mm) não
#: cabia nos 52 mm úteis de profundidade.
PCB_L = PCB_P = 120.0

#: Zonas: (nome, x, y, largura, profundidade, nota)
#:
#: Eixos: x cresce para a direita, y cresce para a TRASEIRA. Logo y=0 é a borda
#: voltada ao motorista e y=100 é onde ficam os vãos do cartão e do USB.
PCB_ZONAS = [
    ("conversor 12V", 6.0, 20.0, CONVERSOR["largura"], CONVERSOR["comprimento"],
     "de pe, girado 90 graus; longe da antena"),
    ("Pico 2 W", 34.0, 66.0, 21.0, 51.0,
     "USB na traseira — faceado"),
    ("leitor microSD", 62.0, 94.0, LEITOR_SD["comprimento"], LEITOR_SD["largura"],
     "boca do cartao na traseira — faceado"),
    ("GPS", 62.0, 10.0, GPS["comprimento"], GPS["largura"],
     "extremidade oposta ao conversor"),
    ("ANTENA", 70.0, 44.0, ANTENA["comprimento"], ANTENA["largura"],
     "area proibida: terra solido, nada por cima"),
]


def conferencia() -> list[str]:
    """Problemas do arranjo: sobreposição, extravasamento e faceamento.

    Existe porque posição é digitada à mão, e dois retângulos que se cruzam no
    desenho não reclamam sozinhos.
    """
    p = []
    for nome, x, y, w, d, _ in PCB_ZONAS:
        if x < 0 or y < 0 or x + w > PCB_L or y + d > PCB_P:
            p.append(f"{nome}: sai da placa ({x}..{x+w} x {y}..{y+d})")
    for i, a in enumerate(PCB_ZONAS):
        for b in PCB_ZONAS[i + 1:]:
            if (a[1] < b[1] + b[3] and b[1] < a[1] + a[3]
                    and a[2] < b[2] + b[4] and b[2] < a[2] + a[4]):
                p.append(f"sobrepostos: {a[0]} e {b[0]}")
    # Furos de fixação não podem cair sobre zona de módulo.
    r = FURO_PCB_DIAM / 2 + FOLGA_FURO
    for fx, fy in FUROS_PCB:
        if not (r <= fx <= PCB_L - r and r <= fy <= PCB_P - r):
            p.append(f"furo ({fx}, {fy}): perto demais da borda")
        for nome, x, y, w, d, _ in PCB_ZONAS:
            if x - r < fx < x + w + r and y - r < fy < y + d + r:
                p.append(f"furo ({fx}, {fy}) colide com {nome}")

    # Faceados: têm de encostar na traseira (y + profundidade perto de PCB_P).
    for alvo in ("Pico 2 W", "leitor microSD"):
        z = next(z for z in PCB_ZONAS if z[0] == alvo)
        folga = PCB_P - (z[2] + z[4])
        if folga > 6.0:
            p.append(f"{alvo}: a {folga:.1f} mm da traseira, nao esta faceado")
    return p


# ------------------------------------------------- fixação da PCB na caixa --

#: Furos de fixação da PLACA na caixa impressa, em coordenada da planta.
#: M3 de passagem (⌀3,2) nas quatro quinas, recuados 6 e 10 mm.
#:
#: As posições não são livres: têm de cair fora de toda zona de módulo. Foi por
#: isso que o conversor subiu 7 mm — a quina inferior esquerda estava ocupada.
FURO_PCB_DIAM = 3.2
FUROS_PCB = [(8.0, 112.0), (112.0, 112.0), (8.0, 8.0), (112.0, 8.0)]

#: Folga mínima entre o furo e qualquer zona de módulo.
FOLGA_FURO = 2.0


def posicao_kicad(x_planta: float, y_planta: float) -> tuple[float, float]:
    """Planta (y cresce para a traseira) para KiCad (y cresce para baixo).

    A traseira, que é onde saem o cartão e o cabo, vira o TOPO do desenho da
    placa — igual ao gabarito impresso, para quem olhar os dois não se perder.
    """
    return (x_planta, PCB_P - y_planta)
