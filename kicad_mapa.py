"""Mapa da netlist do projeto para o vocabulário do KiCad.

Traduz cada peça do `netlist.py` em: referência (designador), símbolo e
footprint. O `netlist.py` continua sendo a fonte — este módulo é só a tradução
para a ferramenta.

## Por que quase tudo é conector

Decisão do autor em 2026-10-05: **todos os módulos em barra de pinos**. A
placa é uma *carrier board* — Pico, GPS, display, leitor microSD, encoder e
conversor plugam nela. O que fica soldado na placa são os passivos, a
proteção de entrada e os conectores.

Isso é o que mantém o aparelho serviçável: módulo com defeito troca puxando,
e o U.FL do GPS — 30 ciclos de encaixe, R-67 — nunca precisa ser tocado.

## O Pico vira DOIS headers, e isso exige cuidado

Um Pico em soquete é fisicamente duas barras de 20 pinos. A netlist usa o
**número de pino físico, de 1 a 40**; no KiCad o segundo header recomeça do 1,
então o físico 21 é o pad 1 de `J2`.

⚠️ Errar esse deslocamento troca metade das ligações **sem gerar erro** — a
netlist fecha, e a placa sai errada. Por isso a tradução é explícita aqui e
tem verificação própria em `verifica()`.

## O índice do Fritzing NÃO é o pad físico

Descoberto ao escrever a verificação: a peça de fusível da biblioteca do
Fritzing **pula o `connector1`** — seus dois terminais são `connector0` e
`connector2`. O índice é propriedade da peça de desenho, não do componente.

Por isso o mapa de pinos aqui é **próprio**, em nome de pino, e não derivado
do `pino()` do Fritzing. Reaproveitar aquele índice funcionaria para a
maioria das peças e produziria uma placa errada na minoria — que é a pior
forma de erro, porque passa despercebida.
"""
from __future__ import annotations

from netlist import NETS, PECAS

# Footprints da biblioteca padrão do KiCad. Os nomes são estáveis desde a v5.
HDR = "Connector_PinHeader_2.54mm:PinHeader_1x{:02d}_P2.54mm_Vertical"

# (ref, simbolo, footprint, n_pinos)
#
# `simbolo` é o nome dentro da biblioteca própria do projeto (`coruja.kicad_sym`),
# gerada junto — assim o projeto não depende da versão da biblioteca do KiCad
# instalada, que é fonte clássica de "abre aqui e não abre aí".
MAPA = {
    # O Pico ocupa dois designadores; ver `pad_do_pico()`.
    "PICO_A": ("J1", "Header_20", HDR.format(20), 20),
    "PICO_B": ("J2", "Header_20", HDR.format(20), 20),

    "GPS":  ("J3", "Header_4", HDR.format(4), 4),
    "SD":   ("J4", "Header_9", HDR.format(9), 9),
    "TFT":  ("J5", "Header_8", HDR.format(8), 8),
    "ENC":  ("J6", "Header_5", HDR.format(5), 5),
    "CONV": ("J7", "Header_4", HDR.format(4), 4),
    "LED":  ("J8", "Header_4", HDR.format(4), 4),
    "BZ":   ("J9", "Header_2", HDR.format(2), 2),

    # Entrada de 12 V: JST-XH de 3 vias, com o pino central sem uso (BOM 19).
    "J12V": ("J10", "Header_3",
             "Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical", 3),

    "F1":   ("F1", "Fusivel",
             "Fuse:Fuseholder_Cylinder-5x20mm_Schurter_0031_8201_Horizontal_Open", 2),
    "TVS1": ("D2", "TVS", "Diode_THT:D_DO-201AD_P12.70mm_Horizontal", 2),
    "D1":   ("D1", "Schottky", "Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal", 2),
    "Q1":   ("Q1", "NPN", "Package_TO_SOT_THT:TO-92_Inline", 3),

    "C1":   ("C1", "Eletrolitico", "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm", 2),
    "C5":   ("C5", "Eletrolitico", "Capacitor_THT:CP_Radial_D13.0mm_P5.00mm", 2),
    "C2":   ("C2", "Ceramico", "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm", 2),

    "R1":   ("R1", "Resistor", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm", 2),
    "R2":   ("R2", "Resistor", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm", 2),
    "R3":   ("R3", "Resistor", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm", 2),
    "R4":   ("R4", "Resistor", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm", 2),
    "R5":   ("R5", "Resistor", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm", 2),
}

#: Quantos pinos físicos tem o Pico, e onde o segundo header começa.
PICO_PINOS = 40
PICO_CORTE = 20


def pad_do_pico(fisico: int) -> tuple[str, int]:
    """Pino físico do Pico (1 a 40) para (referência, pad) no KiCad.

    O físico 21 é o pad 1 do segundo header, não o 21.
    """
    if not 1 <= fisico <= PICO_PINOS:
        raise ValueError(f"pino fisico fora de 1..{PICO_PINOS}: {fisico}")
    if fisico <= PICO_CORTE:
        return MAPA["PICO_A"][0], fisico
    return MAPA["PICO_B"][0], fisico - PICO_CORTE


#: Mapa de pino para pad, para as peças que NÃO são barra de pinos.
#:
#: Nas barras, o pad é a posição do nome na lista de `PECAS` mais um, porque
#: a ordem dos nomes é a ordem física. Nas peças abaixo não é, e cada uma
#: ganha mapa explícito.
#:
#: ⚠️ **Polaridades a conferir quando o KiCad estiver instalado.** O footprint
#: desenha a marca de polaridade num pad específico, e o que está aqui é a
#: convenção esperada, não verificada. Está na lista de pendências do
#: `pcb.md`.
PADS = {
    # Fusível e TVS: dois terminais sem polaridade. Ordem indiferente.
    "F1":   {"0": 1, "1": 2},
    "TVS1": {"0": 1, "1": 2},

    # Schottky: pad 1 é o CATODO, que é onde o footprint do KiCad põe a
    # faixa de serigrafia.
    "D1":   {"K": 1, "A": 2},

    # Eletrolíticos: pad 1 é o POSITIVO nos footprints `CP_Radial`.
    "C1":   {"+": 1, "-": 2},
    "C5":   {"+": 1, "-": 2},
    # Cerâmico não tem polaridade.
    "C2":   {"0": 1, "1": 2},

    # BC337/2N2222 em TO-92_Inline, pads 1-2-3 da esquerda para a direita
    # com a face chata para o observador. **A ordem E-B-C foi MEDIDA** na
    # peça em 2026-10-04 (R-67), por ruptura reversa — não presumida do
    # datasheet, que para esta marcação não identificava a variante.
    "Q1":   {"E": 1, "B": 2, "C": 3},
}

# --- as duas peças de painel, que na placa são conector ---------------------
#
# ⚠️ **Definir estes pads é definir a pinagem do CABO.** Não é detalhe de
# desenho: é o que alguém vai soldar do outro lado. A escolha segue a
# convenção de pôr o comum primeiro.
#
# Quando a placa existir, isto precisa descer para o `bom_schematic.md`, que é
# onde a fiação é revisada.

# Buzzer, 2 vias. O positivo vem do 12 V protegido; o outro lado desce ao
# coletor do Q1.
PADS["BZ"] = {"0": 1, "1": 2}

# LED RGB de ÂNODO comum (R-33). O pino comum se chama "K" porque a peça do
# Fritzing é de catodo comum — o nome ficou, o circuito não: ele vai ao 3V3.
PADS["LED"] = {"K": 1, "R": 2, "G": 3, "B": 4}

# Resistores: dois terminais, sem polaridade.
for _r in ("R1", "R2", "R3", "R4", "R5"):
    PADS[_r] = {"0": 1, "1": 2}


def _pad_por_nome(peca: str, nome_pino: str) -> int:
    """Pad do KiCad para um pino nomeado, sem passar pelo Fritzing."""
    if peca in PADS:
        if nome_pino not in PADS[peca]:
            raise ValueError(f"{peca}.{nome_pino} nao esta no PADS")
        return PADS[peca][nome_pino]
    # Barra de pinos: a ordem dos nomes em PECAS é a ordem física.
    for ident, _, _, _, _, nomes in PECAS:
        if ident == peca:
            if not nomes:
                raise ValueError(f"{peca} nao tem nomes de pino nem mapa em PADS")
            if nome_pino not in nomes:
                raise ValueError(f"{peca}.{nome_pino} nao esta nos nomes da peca")
            return nomes.index(nome_pino) + 1
    raise ValueError(f"peca desconhecida: {peca}")


def no_kicad(peca: str, nome_pino: str) -> tuple[str, int]:
    """Um nó da netlist em (referência, pad) do KiCad."""
    if peca == "PICO":
        return pad_do_pico(int(nome_pino))
    ref, _, _, n = MAPA[peca]
    pad = _pad_por_nome(peca, nome_pino)
    if not 1 <= pad <= n:
        raise ValueError(f"{peca}.{nome_pino} -> pad {pad}, fora de 1..{n}")
    return ref, pad


def titulo_da_peca(peca: str) -> str:
    for ident, _, _, titulo, _, _ in PECAS:
        if ident == peca:
            return titulo
    return peca


def verifica() -> list[str]:
    """Confere que a tradução cobre a netlist inteira, sem colisão.

    Devolve a lista de problemas; vazia significa que a netlist do KiCad vai
    sair com os mesmos nós da do Fritzing.
    """
    problemas: list[str] = []

    # 1. Toda peça da netlist tem tradução.
    usadas = {p for nos in NETS.values() for p, _ in nos}
    for p in sorted(usadas):
        if p != "PICO" and p not in MAPA:
            problemas.append(f"peca sem traducao no MAPA: {p}")

    # 2. Todo nó resolve, e dois nós não caem no mesmo pad.
    ocupados: dict[tuple[str, int], str] = {}
    for rede, nos in NETS.items():
        for peca, nome in nos:
            try:
                alvo = no_kicad(peca, nome)
            except (KeyError, ValueError) as e:
                problemas.append(f"{rede}: {peca}.{nome} -> {e}")
                continue
            anterior = ocupados.get(alvo)
            if anterior is not None and anterior != rede:
                problemas.append(
                    f"pad {alvo[0]}.{alvo[1]} em duas redes: {anterior} e {rede}")
            ocupados[alvo] = rede

    # 3. Designadores únicos.
    vistos: dict[str, str] = {}
    for peca, (ref, *_), in ((k, v) for k, v in MAPA.items()):
        if ref in vistos:
            problemas.append(f"designador {ref} repetido: {vistos[ref]} e {peca}")
        vistos[ref] = peca

    return problemas
