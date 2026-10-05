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

## O Pico é UMA peça, com o footprint oficial

O Pico usa `Module:RaspberryPi_Pico_Common_THT`: 40 pads numerados de 1 a 40,
exatamente como o pino físico. A tradução é a identidade.

**Já foi feito com duas barras de 20 pinos, e era pior por dois motivos.**

O primeiro é que o segundo header recomeça do pad 1, então o físico 21 virava
o pad 1 de `J2` — um deslocamento de 20 que, se errado, troca metade das
ligações **sem gerar erro nenhum**: a netlist fecha e a placa sai errada.

O segundo é mais sério e não tinha conserto por verificação: **a distância
entre as duas fileiras não estava escrita em lugar nenhum.** Dois footprints
independentes podem ser posicionados a qualquer distância na placa, e nada
acusaria 17,0 mm no lugar de 17,78 mm — a placa voltaria da fabricação com os
furos no lugar errado e o Pico não entraria. O footprint oficial carrega as
duas colunas em x=0 e x=17,78 mm por construção.

Trocar duas peças por uma apagou uma classe inteira de erro em vez de
verificá-la, que é sempre o negócio melhor.

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
    # Peça única, footprint oficial: o pad JÁ é o pino físico, e as duas
    # fileiras vêm a 17,78 mm por construção.
    "PICO": ("U1", "Pico", "Module:RaspberryPi_Pico_Common_THT", 40),

    # GY-GPS6MV2 com NEO-M8N. Footprint próprio: espaçador M4 e jumper,
    # com o U.FL marcado na camada de fabricação.
    "GPS":  ("J1", "GPS_NEO_M8N", "coruja:GPS_GY_GPS6MV2", 4),
    # Adafruit 4682. Footprint próprio: a barra de 9 pinos vem com o
    # contorno do módulo e os furos de fixação, que a barra genérica
    # não carrega. Geometria do arquivo da Adafruit, conferida na peça.
    "SD":   ("J2", "Leitor_microSD", "coruja:Leitor_microSD_Adafruit4682", 9),
    "TFT":  ("J3", "Display_ST7789V", HDR.format(8), 8),
    "ENC":  ("J4", "Encoder_KY040", HDR.format(5), 5),
    # Sem barra de pinos: quatro furos nas quinas, para jumper, e dois
    # furos de M3. Footprint próprio, de medida a paquímetro.
    "CONV": ("J5", "Conversor_12V_5V", "coruja:Conversor_LM2596", 4),
    "LED":  ("J6", "LED_RGB_AC", HDR.format(4), 4),
    "BZ":   ("J7", "Buzzer", HDR.format(2), 2),

    # Entrada de 12 V: JST-XH de 3 vias, com o pino central sem uso (BOM 19).
    "J12V": ("J8", "Entrada_12V",
             "Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical", 3),

    "F1":   ("F1", "Fusivel",
             "Fuse:Fuseholder_Cylinder-5x20mm_Schurter_0031_8201_Horizontal_Open", 2),
    "TVS1": ("D2", "TVS", "Diode_THT:D_DO-201AD_P12.70mm_Horizontal", 2),
    "D1":   ("D1", "Schottky", "Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal", 2),
    "Q1":   ("Q1", "NPN", "Package_TO_SOT_THT:TO-92_Inline", 3),

    "C1":   ("C1", "Eletrolitico", "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm", 2),
    "C5":   ("C5", "Eletrolitico", "Capacitor_THT:CP_Radial_D13.0mm_P5.00mm", 2),
    "C2":   ("C2", "Ceramico", "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm", 2),

    "R1":   ("R1", "Resistor", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal", 2),
    "R2":   ("R2", "Resistor", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal", 2),
    "R3":   ("R3", "Resistor", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal", 2),
    "R4":   ("R4", "Resistor", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal", 2),
    "R5":   ("R5", "Resistor", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal", 2),
}

#: Quantos pinos físicos tem o Pico.
PICO_PINOS = 40


def pad_do_pico(fisico: int) -> tuple[str, int]:
    """Pino físico do Pico (1 a 40) para (referência, pad) no KiCad.

    Identidade, desde que o footprint é o oficial. A função continua existindo
    porque é onde a faixa é conferida: um pino 41 vindo da netlist tem de dar
    erro aqui, e não virar um pad silenciosamente inexistente.
    """
    if not 1 <= fisico <= PICO_PINOS:
        raise ValueError(f"pino fisico fora de 1..{PICO_PINOS}: {fisico}")
    return MAPA["PICO"][0], fisico


#: Mapa de pino para pad, para as peças que NÃO são barra de pinos.
#:
#: Nas barras, o pad é a posição do nome na lista de `PECAS` mais um, porque
#: a ordem dos nomes é a ordem física. Nas peças abaixo não é, e cada uma
#: ganha mapa explícito.
#:
#: ✅ **Polaridades verificadas em 2026-10-05**, lendo os próprios arquivos da
#: biblioteca do KiCad 10.0.6 — não mais a convenção presumida. O que foi
#: medido em cada footprint está anotado junto da peça.
PADS = {
    # Fusível: dois terminais sem polaridade. Ordem indiferente.
    "F1":   {"0": 1, "1": 2},

    # TVS: **sem polaridade, e isso é propriedade da PEÇA, não do footprint.**
    # O P6KE24CA é bidirecional (BOM item 27; o sufixo `CA` é obrigatório, e a
    # versão `A` unidirecional montada ao contrário fica em curto permanente).
    #
    # ⚠️ O footprint D_DO-201AD **desenha um `K` em x=0**, ou seja, marca o
    # pad 1 como catodo. Para o `CA` essa marca não significa nada, e a peça
    # real não tem banda correspondente. Fica aqui o registro para que ninguém
    # olhe a serigrafia, procure a banda, não ache e conclua que recebeu a
    # peça errada. Ver pendência no `pcb.md`.
    "TVS1": {"0": 1, "1": 2},

    # Schottky: pad 1 é o CATODO. Verificado — o texto `K` do footprint está
    # em x=0, que é a posição do pad 1 (o pad 2 fica em x=10,16).
    "D1":   {"K": 1, "A": 2},

    # Eletrolíticos: pad 1 é o POSITIVO. Verificado pela geometria, porque o
    # `+` do CP_Radial não é texto e sim desenho: duas linhas de 1,00 mm que
    # se cruzam em (-2,48; -2,88), do lado do pad 1 (x=0) e oposto ao pad 2
    # (x=5).
    "C1":   {"+": 1, "-": 2},
    "C5":   {"+": 1, "-": 2},
    # Cerâmico não tem polaridade.
    "C2":   {"0": 1, "1": 2},

    # BC337/2N2222 em TO-92_Inline, pads 1-2-3 da esquerda para a direita
    # com a face chata para o observador. **A ordem E-B-C foi MEDIDA** na
    # peça em 2026-10-04 (R-67), por ruptura reversa — não presumida do
    # datasheet, que para esta marcação não identificava a variante.
    #
    # ✅ O footprint confere com essa orientação (2026-10-05): o contorno de
    # fabricação tem a RETA em y=+1,75 (lado chato) e os arcos abaulando para
    # y=-2,48 (lado redondo), com os pads em x=0 / 1,27 / 2,54. Quem olha a
    # face chata vê o pad 1 à esquerda — a mesma vista em que a medição foi
    # feita. Montar com o lado chato para o outro lado troca E e C.
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


def pinos(peca: str) -> list[tuple[int, str]]:
    """Pinos do símbolo da peça, como `(pad, nome)`, em ordem de pad.

    **Uma fonte só:** o mesmo dado que resolve a netlist desenha o símbolo. Se
    fossem dois, o esquemático acabaria mostrando um pino que a netlist não
    liga — e um pino solto no desenho não chama atenção de ninguém.

    No Pico, nome e número coincidem — é o pino físico, de 1 a 40. Coincidem
    porque o footprint é o oficial; com duas barras de 20 não coincidiam, e
    essa diferença era uma fonte de erro silencioso.
    """
    if peca == "PICO":
        return [(i, str(i)) for i in range(1, PICO_PINOS + 1)]
    if peca in PADS:
        return sorted((pad, nome) for nome, pad in PADS[peca].items())
    for ident, _, _, _, _, nomes in PECAS:
        if ident == peca and nomes:
            return [(i + 1, nome) for i, nome in enumerate(nomes)]
    raise ValueError(f"sem pinos para {peca}")


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

    # 4. O símbolo tem um pino para cada pad do footprint, numerados 1..n.
    #    Um símbolo com menos pinos que o footprint deixa pad sem ligação, e a
    #    placa sai com uma ilha solta que nenhuma verificação elétrica acusa.
    for peca, (ref, _, _, n) in MAPA.items():
        try:
            lista = pinos(peca)
        except ValueError as e:
            problemas.append(f"{peca}: {e}")
            continue
        if len(lista) != n:
            problemas.append(
                f"{peca}: simbolo com {len(lista)} pinos, footprint com {n} pads")
        pads = {pad for pad, _ in lista}
        if pads != set(range(1, n + 1)):
            problemas.append(f"{peca}: pads do simbolo nao sao 1..{n}: {sorted(pads)}")

    return problemas
