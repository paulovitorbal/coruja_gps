"""Gera os footprints próprios, em `kicad/coruja.pretty/`.

A biblioteca do KiCad traz footprint de **componente**; vários dos nossos são
**placas de breakout** genéricas, sem padrão de fabricante. Para essas, o
footprint sai daqui — e só com medida tirada na peça (`medidas.py`).

## Por que não basta a barra de pinos genérica

Um `PinHeader_1x04` tem os furos e nada mais: sem contorno, sem área de
ocupação. Na hora de posicionar, nada impede um módulo ficar por cima do outro,
porque para a verificação de projeto só existem furos.

O footprint próprio carrega o **contorno medido**, e aí a sobreposição vira
erro em vez de surpresa na bancada.

## O conversor não tem pino nenhum

Descoberto medindo, em 2026-10-05: as conexões são quatro furos nas quinas,
para fio, e não uma barra. O módulo fica **apoiado em espaçador de 2 mm** e é
ligado por jumper.

Isso muda o que o footprint precisa ser. A posição dos furos do módulo **não
entra no desenho** — o fio é flexível e absorve a diferença, e foi por isso que
o conflito de 0,70 mm na medição deles pôde ser encerrado sem resolver. O que
entra é o que é rígido: os dois furos de M3, o contorno e a altura.

### As ilhas ficam FORA do contorno, de propósito

Com 2 mm de folga não se enfia ferro de solda debaixo do módulo. Ilha embaixo
obrigaria a soldar o lado da placa antes de encaixar o módulo, e qualquer
manutenção depois viraria desmonte. Fora do contorno, os dois lados do jumper
continuam acessíveis com tudo montado.
"""
from __future__ import annotations

import pathlib

from expressao_s import Txt, despeja
from kicad_mapa import titulo_da_peca
from medidas import CONVERSOR, GPS, LEITOR_SD

VERSAO = "20251024"

#: Traço de serigrafia e de contorno, nas larguras que a biblioteca usa.
SILK = 0.12
FAB = 0.1
CRT = 0.05
#: Folga da área de ocupação em volta de tudo.
FOLGA = 0.25

#: Ilha de jumper: furo e cobre. Fio de 22 AWG tem ~0,64 mm; 1,0 mm de furo
#: entra com folga e ainda aceita 20 AWG na entrada de 12 V.
JUMPER_FURO = 1.0
JUMPER_COBRE = 2.0


def _t(x: float) -> str:
    return f"{x:g}"


def _linha(x1, y1, x2, y2, camada, larg) -> list:
    return ["fp_line",
            ["start", _t(x1), _t(y1)], ["end", _t(x2), _t(y2)],
            ["stroke", ["width", _t(larg)], ["type", "solid"]],
            ["layer", Txt(camada)]]


def _retangulo(meia_l, meia_a, camada, larg) -> list[list]:
    x, y = meia_l, meia_a
    return [_linha(-x, -y, x, -y, camada, larg),
            _linha(x, -y, x, y, camada, larg),
            _linha(x, y, -x, y, camada, larg),
            _linha(-x, y, -x, -y, camada, larg)]


def _texto(tipo: str, valor: str, x, y, camada, tam=1.0, esp=0.15) -> list:
    return ["property", Txt(tipo), Txt(valor),
            ["at", _t(x), _t(y), "0"],
            ["layer", Txt(camada)],
            ["effects", ["font", ["size", _t(tam), _t(tam)],
                         ["thickness", _t(esp)]]]]


def _rotulo(valor: str, x, y, camada="F.SilkS", tam=0.8) -> list:
    return ["fp_text", "user", Txt(valor),
            ["at", _t(x), _t(y), "0"],
            ["layer", Txt(camada)],
            ["effects", ["font", ["size", _t(tam), _t(tam)],
                         ["thickness", "0.12"]]]]


def _poligono(pontos, camada, larg) -> list[list]:
    """Contorno fechado, segmento a segmento."""
    saida = []
    for i, (x1, y1) in enumerate(pontos):
        x2, y2 = pontos[(i + 1) % len(pontos)]
        saida.append(_linha(x1, y1, x2, y2, camada, larg))
    return saida


def leitor_sd() -> list:
    """Adafruit 4682. Geometria do arquivo EAGLE da Adafruit (CC-BY-SA),
    conferida contra a peça física com o gabarito 1:1 em 2026-10-05: os nove
    pinos e os dois furos de fixação bateram.

    A peça tem as quinas ARREDONDADAS, não chanfradas como o arquivo desenha.
    O contorno de serigrafia segue o arquivo; a área de ocupação usa o
    RETÂNGULO ENVOLVENTE, que limita os dois casos — o arredondamento avança
    ~0,74 mm além do chanfro no meio da quina, e a área de ocupação existe
    justamente para não deixar ninguém encostar coisa ali.
    """
    m = LEITOR_SD
    # Origem no pino 1, como é costume em conector. O módulo tem o pino 1 em
    # (2,54; 2,54) e o Y dele cresce para cima; o do footprint, para baixo.
    def conv(mx, my):
        return (mx - 2.54, -(my - 2.54))

    contorno = [conv(x, y) for x, y in m["contorno"]]
    xs = [x for x, _ in contorno]
    ys = [y for _, y in contorno]

    fp: list = [
        "footprint", Txt("Leitor_microSD_Adafruit4682"),
        ["version", VERSAO],
        ["generator", Txt("coruja_gps")],
        ["generator_version", Txt("10.0")],
        ["layer", Txt("F.Cu")],
        ["descr", Txt(
            f"Adafruit 4682 microSD SPI/SDIO, {_t(m['comprimento'])} x "
            f"{_t(m['largura'])} mm, barra de 9 pinos passo 2,54. Geometria do "
            "arquivo EAGLE da Adafruit (Limor Fried/Ladyada, CC-BY-SA), "
            "conferida contra a peca em 2026-10-05. A boca do cartao fica na "
            "borda oposta ao conector.")],
        ["tags", Txt("microsd adafruit 4682 modulo coruja_gps")],
        _texto("Reference", "REF**", 10.16, 5.2, "F.SilkS"),
        _texto("Value", "Leitor_microSD_Adafruit4682", 10.16, -23.0, "F.Fab"),
        _texto("Datasheet", "", 0, 0, "F.Fab"),
        _texto("Description", titulo_da_peca("SD"), 0, 0, "F.Fab"),
        ["attr", "through_hole"],
    ]

    fp += _poligono(contorno, "F.SilkS", SILK)
    fp += _poligono(contorno, "F.Fab", FAB)
    # Área de ocupação: retângulo envolvente, não o contorno chanfrado.
    fp += [_linha(min(xs) - FOLGA, min(ys) - FOLGA, max(xs) + FOLGA, min(ys) - FOLGA, "F.CrtYd", CRT),
           _linha(max(xs) + FOLGA, min(ys) - FOLGA, max(xs) + FOLGA, max(ys) + FOLGA, "F.CrtYd", CRT),
           _linha(max(xs) + FOLGA, max(ys) + FOLGA, min(xs) - FOLGA, max(ys) + FOLGA, "F.CrtYd", CRT),
           _linha(min(xs) - FOLGA, max(ys) + FOLGA, min(xs) - FOLGA, min(ys) - FOLGA, "F.CrtYd", CRT)]

    # Furos de fixação do módulo, sem cobre. Opcionais.
    # ⚠️ Usá-los exige espaçador da MESMA altura da pilha do conector; os dois
    # brigam se não casarem. Furo não usado não custa nada; furo que falta
    # custa uma placa nova.
    for mx, my in m["furos"]:
        fx, fy = conv(mx, my)
        fp.append(["pad", Txt(""), "np_thru_hole", "circle",
                   ["at", _t(fx), _t(fy)],
                   ["size", _t(m["furo_diam"]), _t(m["furo_diam"])],
                   ["drill", _t(m["furo_diam"])],
                   ["layers", Txt("F&B.Cu"), Txt("*.Mask")]])

    # Os nove pinos. A ordem é a do netlist.py, que casa pad a pad com a
    # serigrafia da Adafruit — conferido em 2026-10-05.
    for i, (mx, my, nome) in enumerate(m["pinos"], start=1):
        fx, fy = conv(mx, my)
        fp.append(["pad", Txt(str(i)), "thru_hole",
                   "rect" if i == 1 else "circle",
                   ["at", _t(fx), _t(fy)],
                   ["size", "1.7", "1.7"],
                   ["drill", _t(m["pino_furo"])],
                   ["layers", Txt("*.Cu"), Txt("*.Mask")]])
        fp.append(_rotulo(nome, fx, fy + 3.2, "F.Fab", 0.7))

    fp.append(_rotulo("boca do cartao ->", 10.16, -21.6, "F.Fab", 0.9))
    fp.append(["embedded_fonts", "no"])
    return fp


def gps() -> list:
    """GY-GPS6MV2 com u-blox NEO-M8N. Medido a paquímetro em 2026-10-05;
    contorno conferido contra a peça com o gabarito 1:1.

    Montagem por ESPAÇADOR e ligação por JUMPER, como o conversor — então as
    ilhas ficam fora do contorno, porque não se solda sob um módulo apoiado.

    A posição dos quatro pinos foi medida e está no `medidas.py`, mas não entra
    aqui: com jumper, o fio absorve. Entra se um dia o módulo for plugado.

    O U.FL é marcado na camada de fabricação. Não é furo nem ilha — é por onde
    sai o rabicho de 8 cm, e quem posicionar o módulo na placa precisa saber
    disso antes de encostá-lo numa parede (R-67).
    """
    m = GPS
    c, l = m["comprimento"], m["largura"]
    meia_c, meia_l = c / 2, l / 2

    def conv(mx, my):
        return (mx - meia_c, -(my - meia_l))

    ilha_x = meia_c + 2.68

    fp: list = [
        "footprint", Txt("GPS_GY_GPS6MV2"),
        ["version", VERSAO],
        ["generator", Txt("coruja_gps")],
        ["generator_version", Txt("10.0")],
        ["layer", Txt("F.Cu")],
        ["descr", Txt(
            f"Modulo GPS GY-GPS6MV2 com u-blox NEO-M8N, {_t(c)} x {_t(l)} mm. "
            f"Montado em espacador (4 furos M4, vao 28 x 18 mm) e ligado por "
            "jumper; as ilhas ficam FORA do contorno porque nao se solda sob o "
            "modulo. U.FL marcado na camada de fabricacao: o rabicho tem 8 cm. "
            "Medido a paquimetro e conferido em gabarito 1:1, 2026-10-05.")],
        ["tags", Txt("gps gy-gps6mv2 neo-m8n ublox modulo coruja_gps")],
        _texto("Reference", "REF**", 0, -meia_l - 2.6, "F.SilkS"),
        _texto("Value", "GPS_GY_GPS6MV2", 0, meia_l + 2.6, "F.Fab"),
        _texto("Datasheet", "", 0, 0, "F.Fab"),
        _texto("Description", titulo_da_peca("GPS"), 0, 0, "F.Fab"),
        ["attr", "through_hole"],
    ]

    fp += _retangulo(meia_c, meia_l, "F.SilkS", SILK)
    fp += _retangulo(meia_c, meia_l, "F.Fab", FAB)
    fp += _retangulo(ilha_x + JUMPER_COBRE / 2 + FOLGA, meia_l + FOLGA,
                     "F.CrtYd", CRT)

    # Quatro furos de espaçador, sem cobre.
    for mx, my in m["furos"]:
        fx, fy = conv(mx, my)
        fp.append(["pad", Txt(""), "np_thru_hole", "circle",
                   ["at", _t(fx), _t(fy)],
                   ["size", _t(m["furo_diam"]), _t(m["furo_diam"])],
                   ["drill", _t(m["furo_diam"])],
                   ["layers", Txt("F&B.Cu"), Txt("*.Mask")]])

    # Ilhas do jumper, uma na frente de cada pino, fora do contorno.
    for i, (_mx, my, nome) in enumerate(m["pinos"], start=1):
        _, fy = conv(0, my)
        fp.append(["pad", Txt(str(i)), "thru_hole",
                   "rect" if i == 1 else "circle",
                   ["at", _t(ilha_x), _t(fy)],
                   ["size", _t(JUMPER_COBRE), _t(JUMPER_COBRE)],
                   ["drill", _t(JUMPER_FURO)],
                   ["layers", Txt("*.Cu"), Txt("*.Mask")]])
        fp.append(_rotulo(nome, ilha_x + 2.6, fy))

    # U.FL: só marcação de fabricação.
    ux, uy = conv(*m["ufl"])
    fp.append(["fp_circle",
               ["center", _t(ux), _t(uy)], ["end", _t(ux + 1.5), _t(uy)],
               ["stroke", ["width", _t(FAB)], ["type", "solid"]],
               ["fill", "none"], ["layer", Txt("F.Fab")]])
    fp.append(_rotulo("U.FL (rabicho 8cm)", ux + 10.0, uy, "F.Fab", 0.9))

    fp.append(["embedded_fonts", "no"])
    return fp


def conversor() -> list:
    m = CONVERSOR
    c, l = m["comprimento"], m["largura"]
    meia_c, meia_l = c / 2, l / 2
    raio_m3 = m["furo_m3_diam"] / 2

    # Origem no CENTRO do contorno. Os dois furos de M3 são simétricos a 180°
    # em torno dele — foi o que o gabarito 1:1 confirmou —, então o centro é a
    # origem que não privilegia uma quina sobre a outra.
    m3x = meia_c - m["furo_m3_x"]
    m3y = meia_l - m["furo_m3_y"]

    # As ilhas ficam uma coluna à esquerda e outra à direita do contorno.
    # OUT no lado -x e IN no lado +x: o gabarito mostrou o furo de M3 de uma
    # quina junto do OUT- e o da quina oposta junto do IN+.
    ilha_x = meia_c + 2.68
    ilha_y = 8.89
    #                número, nome,   x,         y
    ilhas = [("1", "IN+",  ilha_x,  ilha_y),
             ("2", "IN-",  ilha_x, -ilha_y),
             ("3", "OUT+", -ilha_x, ilha_y),
             ("4", "OUT-", -ilha_x, -ilha_y)]

    fp: list = [
        "footprint", Txt("Conversor_LM2596"),
        ["version", VERSAO],
        ["generator", Txt("coruja_gps")],
        ["generator_version", Txt("10.0")],
        ["layer", Txt("F.Cu")],
        ["descr", Txt(
            f"Modulo step-down 12V->5V, {_t(c)} x {_t(l)} x {_t(m['altura'])} mm, "
            f"apoiado em espacador de {_t(m['espacador_altura'])} mm. "
            "Ligado por jumper: as ilhas ficam FORA do contorno porque nao se "
            "solda sob o modulo. Medido a paquimetro em 2026-10-05.")],
        ["tags", Txt("conversor step-down lm2596 modulo coruja_gps")],
        _texto("Reference", "REF**", 0, -meia_l - 2.6, "F.SilkS"),
        _texto("Value", "Conversor_LM2596", 0, meia_l + 2.6, "F.Fab"),
        _texto("Datasheet", "", 0, 0, "F.Fab"),
        _texto("Description", titulo_da_peca("CONV"), 0, 0, "F.Fab"),
        ["attr", "through_hole"],
    ]

    # Contorno: serigrafia para quem monta, fabricação para quem confere.
    fp += _retangulo(meia_c, meia_l, "F.SilkS", SILK)
    fp += _retangulo(meia_c, meia_l, "F.Fab", FAB)
    # Área de ocupação envolve contorno E ilhas — é ela que acusa sobreposição.
    fp += _retangulo(ilha_x + JUMPER_COBRE / 2 + FOLGA, meia_l + FOLGA,
                     "F.CrtYd", CRT)

    # Furos de fixação: sem cobre (NPTH). São eles que seguram a placa.
    for sx, sy in ((-m3x, -m3y), (m3x, m3y)):
        fp.append(["pad", Txt(""), "np_thru_hole", "circle",
                   ["at", _t(sx), _t(sy)],
                   ["size", _t(m["furo_m3_diam"]), _t(m["furo_m3_diam"])],
                   ["drill", _t(m["furo_m3_diam"])],
                   ["layers", Txt("F&B.Cu"), Txt("*.Mask")]])

    # Ilhas do jumper. A 1 é retangular, convenção de "primeira".
    for num, nome, px, py in ilhas:
        fp.append(["pad", Txt(num), "thru_hole",
                   "rect" if num == "1" else "circle",
                   ["at", _t(px), _t(py)],
                   ["size", _t(JUMPER_COBRE), _t(JUMPER_COBRE)],
                   ["drill", _t(JUMPER_FURO)],
                   ["layers", Txt("*.Cu"), Txt("*.Mask")]])
        # Rótulo do lado de fora, longe do contorno.
        dx = 2.4 if px > 0 else -2.4
        fp.append(_rotulo(nome, px + dx, py))

    # Altura, para quem for conferir a folga dentro da caixa.
    fp.append(_rotulo(f"h={_t(m['altura'])}+{_t(m['espacador_altura'])}mm",
                      0, 0, "F.Fab", 1.0))
    fp.append(["embedded_fonts", "no"])
    return fp


def main() -> None:
    destino = pathlib.Path(__file__).parent / "kicad" / "coruja.pretty"
    destino.mkdir(parents=True, exist_ok=True)
    for construtor in (conversor, gps, leitor_sd):
        fp = construtor()
        nome = fp[1]
        (destino / f"{nome}.kicad_mod").write_text(despeja(fp) + "\n")
        print(f"{destino}/{nome}.kicad_mod")


if __name__ == "__main__":
    main()
