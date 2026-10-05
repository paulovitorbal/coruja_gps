"""Gera `coruja.kicad_sym`: a biblioteca de símbolos própria do projeto.

## Por que biblioteca própria

O projeto não depende da versão da biblioteca do KiCad instalada. Nome de
símbolo muda entre versões, e um projeto que referencia `Device:R` abre numa
máquina e não abre na outra. Com a biblioteca junto do projeto, o que abre é
sempre o mesmo desenho.

Para *footprint* a escolha é oposta — ali usamos a biblioteca padrão, porque o
footprint carrega geometria de fabricação que não convém reinventar, e os
nomes daquela biblioteca são estáveis. A conferência de que cada um existe
está em `verifica_kicad.py`.

## Por que um símbolo por peça, e não `Header_4` genérico

Porque o esquemático é para ser **lido por gente** — é o par do
`bom_schematic.md`, que é onde a fiação é revisada. Um conector cujos pinos se
chamam `1 2 3 4` não diz nada; um que se chama `VCC 5V / RX / TX / GND` deixa
a troca de RX com TX saltar aos olhos. O custo é gerar 16 símbolos em vez de
6, e eles são gerados.

## A forma de todos: caixa com pinos à esquerda

Uniforme de propósito. A ligação no esquemático é feita por **rótulo de rede**,
não por fio roteado, então a geometria do símbolo não precisa sugerir
topologia — precisa ser previsível. Um resistor desenhado como caixa é menos
bonito que o zigue-zague e tem a mesma informação.
"""
from __future__ import annotations

import pathlib

from kicad_mapa import MAPA, pinos, titulo_da_peca
from expressao_s import Txt, despeja

VERSAO = "20251024"

#: Passo entre pinos e tamanho da fonte — a grade de 2,54 mm do KiCad. Sair
#: dela faz o fio não encostar no pino, e o erro é invisível no desenho.
PASSO = 2.54
FONTE = 1.27

#: Largura mínima da caixa, e quanto cada caractere do nome ocupa. O nome é
#: desenhado DENTRO da caixa, então a caixa precisa caber o maior nome.
LARG_MIN = 12.7
LARG_CARACTERE = 1.5


def _n(x: float) -> str:
    """Número como o KiCad escreve: sem zero à toa."""
    return f"{x:g}"


def _efeitos(oculto: bool = False) -> list:
    e = ["effects", ["font", ["size", _n(FONTE), _n(FONTE)]]]
    if oculto:
        e.append(["hide", "yes"])
    return e


def _propriedade(nome: str, valor: str, y: float, oculta: bool = False) -> list:
    p: list = [
        "property", Txt(nome), Txt(valor),
        ["at", "0", _n(y), "0"],
        ["show_name", "no"],
        ["do_not_autoplace", "no"],
    ]
    if oculta:
        p.append(["hide", "yes"])
    p.append(_efeitos())
    return p


def _prefixo(ref: str) -> str:
    """Designador sem o número: `J10` vira `J`, para o KiBot numerar sozinho."""
    return ref.rstrip("0123456789") or "U"


def simbolo(peca: str) -> list:
    """Um símbolo: caixa, pinos à esquerda, propriedades."""
    ref, nome, _footprint, n = MAPA[peca]
    lista = pinos(peca)

    largura = max(LARG_MIN,
                  max(len(txt) for _, txt in lista) * LARG_CARACTERE + PASSO)
    altura = (len(lista) + 1) * PASSO
    meia_l, meia_a = largura / 2, altura / 2

    corpo: list = [
        "symbol", Txt(f"{nome}_0_1"),
        ["rectangle",
         ["start", _n(-meia_l), _n(meia_a)],
         ["end", _n(meia_l), _n(-meia_a)],
         ["stroke", ["width", "0.254"], ["type", "default"]],
         ["fill", ["type", "background"]]],
    ]

    desenho: list = ["symbol", Txt(f"{nome}_1_1")]
    # Primeiro pino no topo, descendo — a mesma ordem da barra física.
    topo = meia_a - PASSO
    for i, (pad, txt) in enumerate(lista):
        y = topo - i * PASSO
        desenho.append([
            "pin", "passive", "line",
            ["at", _n(-meia_l - PASSO), _n(y), "0"],
            ["length", _n(PASSO)],
            ["name", Txt(txt), _efeitos()],
            ["number", Txt(str(pad)), _efeitos()],
        ])

    return [
        "symbol", Txt(nome),
        # `pin_numbers` só aceita `hide`; o `offset` é do `pin_names`. Trocar
        # os dois faz o KiCad recusar a biblioteca INTEIRA, com a mensagem
        # genérica "não foi possível carregar" e sem dizer onde. (2026-10-05)
        ["pin_numbers", ["hide", "no"]],
        ["pin_names", ["offset", "0.508"]],
        ["exclude_from_sim", "no"],
        ["in_bom", "yes"],
        ["on_board", "yes"],
        ["in_pos_files", "yes"],
        ["duplicate_pin_numbers_are_jumpers", "no"],
        _propriedade("Reference", _prefixo(ref), meia_a + PASSO),
        _propriedade("Value", nome, meia_a + PASSO - FONTE * 1.5),
        # Footprint fica VAZIO de propósito: `Eletrolitico` serve a C1 (D10) e
        # a C5 (D13), que têm footprints diferentes. Quem define é a instância
        # no esquemático, a partir do MAPA.
        _propriedade("Footprint", "", 0, oculta=True),
        _propriedade("Datasheet", "", 0, oculta=True),
        _propriedade("Description", titulo_da_peca(peca), 0, oculta=True),
        corpo,
        desenho,
        ["embedded_fonts", "no"],
    ]


def representantes() -> dict[str, str]:
    """Nome do símbolo -> peça que o define.

    `Resistor` serve a R1..R5 e `Eletrolitico` a C1 e C5. Qual das peças
    desenha o símbolo é arbitrário, mas precisa ser **a mesma** aqui e no
    esquemático: o KiCad compara a cópia embutida no `.kicad_sch` com a da
    biblioteca e acusa divergência até na descrição.
    """
    vistos: dict[str, str] = {}
    for peca, (_, nome, _, _) in MAPA.items():
        vistos.setdefault(nome, peca)
    return vistos


def biblioteca() -> list:
    """A biblioteca inteira, um símbolo por nome distinto do MAPA."""
    vistos = representantes()

    lib: list = [
        "kicad_symbol_lib",
        ["version", VERSAO],
        ["generator", Txt("coruja_gps")],
        ["generator_version", Txt("10.0")],
    ]
    for nome in sorted(vistos):
        lib.append(simbolo(vistos[nome]))
    return lib


def main() -> None:
    destino = pathlib.Path(__file__).parent / "kicad" / "coruja.kicad_sym"
    destino.parent.mkdir(exist_ok=True)
    destino.write_text(despeja(biblioteca()) + "\n")
    n = sum(1 for f in biblioteca() if isinstance(f, list) and f[0] == "symbol")
    print(f"{destino}: {n} simbolos")


if __name__ == "__main__":
    main()
