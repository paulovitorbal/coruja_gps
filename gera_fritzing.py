#!/usr/bin/env python3
"""
gera_fritzing.py — gera coruja_gps.fzz a partir da tabela de ligações

Produz um sketch do Fritzing (1.0.x) com o circuito do detector de radares.
A fonte da verdade é a tabela NETS abaixo, transcrita de bom_schematic.md;
mudou a fiação, edite aqui e regenere.

Os moduleId e os IDs de conector foram VERIFICADOS contra a biblioteca
instalada em /Applications/Fritzing.app (2.565 peças) em 2026-09-17.

Quatro módulos não têm peça na biblioteca (NEO-M8N, display, breakout microSD
Adafruit, KY-040) e são representados por headers fêmea genéricos com a
contagem correta de pinos e rótulo no título. A peça do microSD que existe na
biblioteca tem ZERO conectores — é inutilizável.

VISTA PROTOBOARD: fios reais, com geometria calculada a partir da posição
verdadeira de cada pino. As posições vêm de fritzing_geo.py, que lê o SVG de
breadboard de cada peça, resolve o mapeamento connector->svgId do .fzp e aplica
os transforms de grupo. A escala da cena (90 unidades por polegada) foi medida
empiricamente contra o sketch jogo-perguntas.fzz, com erro zero em 6 amostras.

Cores dos fios por rede (CORES abaixo): preto = GND, vermelho = 5V,
azul = 3V3, e cores distintas por grupo de sinal para facilitar o rastreio
durante a soldagem na perfboard.

VISTAS ESQUEMÁTICO E PCB: conexões diretas peça-a-peça, sem fio. O Fritzing
desenha o ratsnest nos pinos certos. Use "Rotear" se quiser traços sólidos.

    python3 gera_fritzing.py
"""
import os
import shutil
import sys
import argparse
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fritzing_geo as geo

AQUI = Path(__file__).resolve().parent
SAIDA_PADRAO = AQUI / "coruja_gps.fzz"

# A peça do Pico 2 W não vem na biblioteca padrão do Fritzing: ela é copiada de
# um sketch que já a tenha. O .fzz versionado deste projeto já a carrega
# embutida, então ele serve de fonte e o gerador fica autossuficiente para quem
# clona o repositório. O sketch externo fica como alternativa.
FONTES_DA_PECA_PICO = [
    AQUI / "coruja_gps.fzz",
    Path.home() / "Documents" / "Fritzing" / "jogo-perguntas.fzz",
]
FRITZING_VERSION = "1.0.7.2026-04-14.CD-2576-0-394a8bb4"

# ---------------------------------------------------------------- peças ------
# (id_local, moduleId, arquivo_fzp, título, propriedades, nomes_de_pino)
PECAS = [
    ("PICO", "rpi_pico-tht_1", "part.rpi_pico-tht_1.fzp",
     "Raspberry Pi Pico 2 W", {}, None),

    ("GPS", "b799cea1-1ee1-11de-8283-0019d2b7521e",
     "generic-female-header-rounded_4.fzp",
     # Ordem conferida na foto do anuncio do modulo comprado (2026-09-18):
     # VCC, RX, TX, GND. A suposicao anterior era VCC, GND, TX, RX -- os pinos
     # 2 e 4 estavam trocados. A CONFIRMAR na serigrafia quando a placa chegar,
     # como foi feito com o KY-040 e o leitor SD, que ambos divergiam. Ver R-34.
     "GPS NEO-M8N (GY-GPSV3)", {}, ["VCC 5V", "RX", "TX", "GND"]),

    # Pinagem RELIDA na placa física em 2026-09-20: são **9 pinos**, da
    # esquerda para a direita olhando de frente. A revisão anterior listava 8
    # e omitia o `D1`, o que deslocava as duas últimas posições — quem contasse
    # posições pela tabela poria o fio do `DET` em `DAT2`. Ver R-40.
    #
    # Três pinos ficam sem ligação — ver a nota junto aos nomes.
    ("SD", "1d2d699b-1ee0-11de-8283-0019d2b7521e",
     "generic-female-header_9.fzp",
     "Leitor microSD (9 pinos)", {},
     # `D1`, `DAT2` e `DET` ficam desconectados. O DET saiu com o card
     # detect (ADR 0010); os outros dois nunca foram usados em modo SPI.
     ["3V", "GND", "CLK", "D0", "CMD", "D3", "D1", "DAT2", "DET"]),

    ("TFT", "17898e57-1ee0-11de-8283-0019d2b7521e",
     "generic-female-header_8.fzp",
     # 2,4" 320x240 confirmado pelo autor em 2026-09-17. O CONTROLADOR ainda
     # nao: 2,4" costuma ser ILI9341 e nao ST7789, e a sequencia de
     # inicializacao difere. A ORDEM DOS PINOS tambem esta a confirmar na
     # serigrafia -- dos tres modulos ja conferidos fisicamente, dois
     # divergiam do documentado (ver R-34).
     'Display 2,4" 320x240 (controlador a confirmar)', {},
     ["GND", "VCC 3V3", "SCL", "SDA", "RES", "DC", "CS", "BL"]),

    # Pinagem conferida na placa física (2026-09-17), da esquerda para a
    # direita olhando de frente. ATENÇÃO: é o inverso da ordem que se
    # costuma ver documentada para o KY-040.
    ("ENC", "bd523905-1ee1-11de-8283-0019d2b7521e",
     "generic-female-header-rounded_5.fzp",
     "Encoder KY-040", {}, ["GND", "+ 3V3", "SW", "DT", "CLK"]),

    # ENTRADA DE 12 V, do pós-chave. O que entra no aparelho passou a ser 12 V
    # e não mais 5 V: o buzzer agora é alimentado em 12 V (SFM-20B é 3-24 V),
    # o que exige 12 V dentro do gabinete de qualquer forma. Com o 12 V já
    # dentro, o conversor vem junto e o cabo externo de 5 V desaparece.
    #
    # Três vias, pino central sem uso: mantém a contagem diferente da do
    # conector do buzzer, que é de 2. Ver a nota de segurança no README.
    ("J12V", "b13c0353-1ee1-11de-8283-0019d2b7521e",
     "generic-female-header-rounded_3.fzp",
     "Entrada 12V (pos-chave)", {},
     ["+12V", "n/c", "GND"]),

    # Fusível de 2 A. Protege o FIO, não a carga — por isso acompanha a
    # corrente do aparelho e não a capacidade do cabo. Fica no chicote, antes
    # da proteção, porque o modo de falha desejável de um TVS é curto.
    ("F1", "SparkFun-Passives-FUSE-X20MM",
     "sparkfun-passives-fuse-x20mm.fzp",
     "F1 2 A (no chicote)", {}, ["0", "1"]),

    # TVS BIDIRECIONAL de 24 V. O sufixo CA é o que o torna bidirecional; a
    # versão A montada ao contrário fica em curto permanente. Clampa em
    # 33,2 V: acima dos 14,4 V da rede com motor ligado, abaixo dos 40 V do
    # conversor. Não cobre load dump real (>1 kW) -- ver README.
    ("TVS1", "SparkFun-DiscreteSemi-TVS-",
     "sparkfun-discretesemi-tvs-.fzp",
     "TVS1 P6KE24CA bidirecional", {}, ["0", "1"]),

    # Conversor step-down. Entrada >= 40 V é requisito, não preferência: o
    # transiente que destrói eletrônica automotiva é o load dump, não a
    # partida. NÃO pode ser isolado -- o retorno do buzzer em 12 V passa pelo
    # emissor do Q1, que está no GND do aparelho.
    ("CONV", "b799cea1-1ee1-11de-8283-0019d2b7521e",
     "generic-female-header-rounded_4.fzp",
     "Conversor step-down 12V->5V (LM2596, nao isolado)", {},
     ["IN+", "IN-", "OUT+", "OUT-"]),

    # NÃO HÁ CONECTOR DO BUZZER. Os dois fios saem da placa e vão soldados
    # direto ao SFM-27, montado no painel da caixa (2026-09-25).
    #
    # Isso apaga um risco em vez de administrá-lo. Enquanto existia um header
    # de 2 vias com 12 V, a única proteção contra encaixá-lo na entrada era a
    # contagem de pinos, e header genérico não é polarizado. Agora o aparelho
    # tem **um único conector externo** (J12V), e não há par para trocar.
    #
    # O preço é servico: para separar a tampa da placa é preciso dessoldar.
    # Ver a nota de montagem no bom_schematic.md.

    ("LED", "d4d5af9700923b8a114f57961f29a8a0ColorLEDModuleID",
     "led-rgb-4pin-cathode_v5.fzp",
     "LED RGB 10mm anodo comum", {}, None),

    ("R1", "ResistorModuleID", "resistor.fzp", "R1 330R (vermelho)",
     {"resistance": "330", "pin spacing": "400 mil"}, None),
    # Valores MEDIDOS na bancada em 2026-09-19, nao calculados: ver R-05.
    # A ordem e o inverso da sensibilidade do olho -- verde precisa do maior
    # resistor porque e o canal mais eficiente por mA.
    ("R2", "ResistorModuleID", "resistor.fzp", "R2 470R (verde)",
     {"resistance": "470", "pin spacing": "400 mil"}, None),
    ("R3", "ResistorModuleID", "resistor.fzp", "R3 150R (azul)",
     {"resistance": "150", "pin spacing": "400 mil"}, None),
    ("R4", "ResistorModuleID", "resistor.fzp", "R4 1k (base Q1)",
     {"resistance": "1k", "pin spacing": "400 mil"}, None),
    ("R5", "ResistorModuleID", "resistor.fzp", "R5 1k (GPIO0 -> GPS RX)",
     {"resistance": "1k", "pin spacing": "400 mil"}, None),

    ("Q1", "SparkFun-DiscreteSemi-TRANSISTOR_NPN-TO92",
     "sparkfun-discretesemi-transistor_npn-to92.fzp",
     "Q1 BC337 (nao BC547)", {}, None),

    ("D1", "145554CBFC44diode", "diode_schottky_1N5817_300mil.fzp",
     "D1 Schottky 1N5817/1N5819", {}, None),
    # NÃO HÁ D2. A roda-livre foi removida em 2026-09-25: o SFM-27 é piezo
    # ativo, carga capacitiva, e um diodo em antiparalelo com carga
    # capacitiva fica reversamente polarizado nos dois estados e nunca
    # conduz. Só serviria contra o pico de desligamento de uma BOBINA.
    #
    # O argumento que restava — proteger uma troca futura por buzzer
    # eletromagnético — caiu junto com o conector: agora a troca exige
    # dessoldar, e o diodo entra nessa hora. Ver R-51.

    ("C1", "MediumElectrolyticCapacitorModuleID",
     "capacitor_electrolytic_medium.fzp",
     "C1 470uF 25V 105C", {"capacitance": "470µF"}, None),
    # Entrada de 12 V, junto ao conversor. 50 V porque a rede automotiva tem
    # transientes; 105 C porque sob o painel se chega a 50-60 C e a vida de um
    # eletrolítico cai pela metade a cada 10 C. Ver L-05.
    ("C5", "MediumElectrolyticCapacitorModuleID",
     "capacitor_electrolytic_medium.fzp",
     "C5 470uF 50V 105C (entrada 12V)", {"capacitance": "470µF"}, None),
    ("C2", "100milCeramicCapacitorModuleID", "capacitor_ceramic_100mil.fzp",
     "C2 100nF (filtro VSYS)", {"capacitance": "100nF"}, None),

    ("BZ", "8abf6496b5d466c7a1893c17a296a676", "piezo sensor.fzp",
     "BZ1 SFM-27 (fios diretos)", {}, None),
]

# Conectores nomeados por peça. Pico: pino fisico N -> connector(N-1).
CONN = {
    # "K" e o pino comum da peca do Fritzing. Como o LED real e de anodo
    # comum, este pino vai ao 3V3 e nao ao GND -- o nome do conector na peca
    # continua "K" porque e a peca de catodo comum da biblioteca.
    "LED": {"R": "connector0", "K": "connector1", "G": "connector2", "B": "connector3"},
    "Q1": {"E": "connector0", "B": "connector1", "C": "connector2"},
    "D1": {"K": "connector0", "A": "connector1"},      # cathode, anode
    "C1": {"-": "connector0", "+": "connector1"},
    "C5": {"-": "connector0", "+": "connector1"},
    # O fusivel da biblioteca pula o connector1: os terminais sao 0 e 2.
    "F1": {"0": "connector0", "1": "connector2"},
    "TVS1": {"0": "connector0", "1": "connector1"},
}


def pino(peca, nome):
    """Resolve (id_local, nome_do_pino) para o connectorId real."""
    if peca == "PICO":
        return f"connector{int(nome) - 1}"
    if peca in CONN:
        return CONN[peca][nome]
    for pid, _mid, _arq, _tit, _props, nomes in PECAS:
        if pid == peca and nomes:
            return f"connector{nomes.index(nome)}"
    return f"connector{int(nome)}"


# ------------------------------------------------------------------ redes ----
# Cada rede é uma lista de (peça, pino). Transcrito de bom_schematic.md rev.2.
NETS = {
    # --- 12 V, do pos-chave ate o conversor e ate o buzzer ------------------
    "12V_ENTRADA":  [("J12V", "+12V"), ("F1", "0")],
    # Depois do fusivel: protecao, conversor e o positivo do buzzer.
    "12V_PROTEGIDO": [("F1", "1"), ("TVS1", "0"), ("C5", "+"),
                      ("CONV", "IN+"), ("BZ", "0")],

    # --- 5 V, gerado dentro do aparelho -------------------------------------
    "5V_CONV":     [("CONV", "OUT+"), ("D1", "A")],
    "VSYS_5V":     [("D1", "K"), ("PICO", "39"), ("C1", "+"), ("C2", "0"),
                    ("GPS", "VCC 5V")],

    # GND comum. O conversor NAO pode ser isolado: sem continuidade entre o
    # negativo de 12 V e o do aparelho, a corrente do buzzer nao fecha pelo
    # emissor do Q1 e ele simplesmente nao toca.
    "GND":         [("J12V", "GND"), ("TVS1", "1"), ("C5", "-"),
                    ("CONV", "IN-"), ("CONV", "OUT-"),
                    ("PICO", "38"), ("PICO", "3"), ("C1", "-"),
                    ("C2", "1"), ("Q1", "E"), ("GPS", "GND"),
                    ("SD", "GND"), ("TFT", "GND"), ("ENC", "GND")],
    # O LED e de ANODO comum (BOM item 8, corrigido em 2026-09-18): o terminal
    # comum vai ao 3V3, nao ao GND, e cada catodo desce por seu resistor ate um
    # GPIO. Ver R-33 -- isso inverte a logica de acionamento no firmware.
    "3V3":         [("PICO", "36"), ("SD", "3V"), ("TFT", "VCC 3V3"),
                    ("ENC", "+ 3V3"), ("LED", "K")],

    "SPI0_SCK":    [("PICO", "24"), ("SD", "CLK"), ("TFT", "SCL")],
    "SPI0_MOSI":   [("PICO", "25"), ("SD", "CMD"), ("TFT", "SDA")],
    "SPI0_MISO":   [("PICO", "21"), ("SD", "D0")],
    "SD_CS":       [("PICO", "22"), ("SD", "D3")],
    # SEM card detect. O pino DET do modulo fica DESCONECTADO desde
    # 2026-09-22 (ADR 0010): o projeto reage igual a cartao ausente e a cartao
    # ilegivel, e a chave do soquete nao abria por completo -- deixava o GPIO
    # 14 em 1,13 V, na zona indeterminada da logica de 3,3 V. O GPIO 14 esta
    # LIVRE.
    "TFT_CS":      [("PICO", "26"), ("TFT", "CS")],
    "TFT_DC":      [("PICO", "27"), ("TFT", "DC")],
    "TFT_RST":     [("PICO", "29"), ("TFT", "RES")],
    "TFT_BL_PWM":  [("PICO", "20"), ("TFT", "BL")],

    "GPS_TX":      [("PICO", "2"), ("GPS", "TX")],
    "UART_TX_R5":  [("PICO", "1"), ("R5", "0")],
    "GPS_RX":      [("R5", "1"), ("GPS", "RX")],

    # SEM capacitor de debounce. Os 100 nF que a revisao 2 mandava instalar
    # IMPEDEM qualquer decodificacao: achatam fases de 45-128 ms em pulsos de
    # 1 ms e apagam o estado (0,0), que e onde a quadratura codifica direcao.
    # Medido na placa. O debounce e a maquina de estados do firmware. R-36.
    "ENC_CLK":     [("PICO", "4"), ("ENC", "CLK")],
    "ENC_DT":      [("PICO", "5"), ("ENC", "DT")],
    "ENC_SW":      [("PICO", "6"), ("ENC", "SW")],

    # ATENCAO: vermelho e azul NAO seguem a ordem crescente de GPIO. A ordem
    # das pernas deste LED de 10 mm nao e R-G-B, e a fiacao foi mantida como
    # construida -- o mapa descreve a placa real. Determinado no modo de
    # calibracao em 2026-09-19: o ambar saia ciano e o rosa saia lilas, o que
    # so acontece com vermelho e azul trocados. Ver R-35.
    #
    # Os resistores acompanham a COR, nao o GPIO: R1=330 no vermelho,
    # R2=470 no verde, R3=150 no azul.
    "LED_R_GPIO":   [("PICO", "11"), ("R1", "0")],   # GPIO 8
    "LED_R_CATODO": [("R1", "1"), ("LED", "R")],
    "LED_G_GPIO":   [("PICO", "10"), ("R2", "0")],   # GPIO 7
    "LED_G_CATODO": [("R2", "1"), ("LED", "G")],
    "LED_B_GPIO":   [("PICO", "9"), ("R3", "0")],    # GPIO 6
    "LED_B_CATODO": [("R3", "1"), ("LED", "B")],

    "BUZZ_GPIO5":   [("PICO", "7"), ("R4", "0")],
    "BUZZ_BASE":    [("R4", "1"), ("Q1", "B")],
    # O buzzer entra direto nas duas redes. Antes havia um conector no meio,
    # mas ele era passa-fio: nao mudava rede nenhuma, so acrescentava dois
    # pinos e um ponto de falha mecanico.
    # Chaveamento pelo lado baixo: o retorno do buzzer NAO vai ao GND por
    # fio, vai pelo transistor. Com o Q1 cortado este no sobe a ~12 V pelo
    # proprio buzzer -- e por isso que, lendo a netlist parada, as duas
    # pernas do buzzer parecem estar no mesmo potencial.
    "BUZZ_COLETOR": [("Q1", "C"), ("BZ", "1")],
}

# Posição no esquemático (unidades internas do Fritzing). Layout em colunas
# por função; ajuste arrastando no Fritzing.
POS = {
    "PICO": (600, 300),
    "GPS": (1100, 180), "SD": (1100, 380), "TFT": (1100, 600), "ENC": (1100, 840),
    "J12V": (60, 60), "F1": (170, 60), "TVS1": (260, 140), "C5": (330, 140),
    "CONV": (120, 240), "D1": (300, 120), "C1": (420, 200), "C2": (500, 200),
    "R1": (250, 560), "R2": (250, 640), "R3": (250, 720), "LED": (80, 640),
    "R4": (250, 900), "Q1": (120, 960), "BZ": (400, 1020),
    "R5": (380, 60),
}

# Paleta padrão do Fritzing. GND/5V/3V3 conforme pedido; o resto por grupo de
# sinal, para dar rastreabilidade visual na perfboard.
CORES = {
    "GND":        "#000000",   # preto
    # ⚠️ 12 V NAO e vermelho, de proposito. A convencao antiga mandava
    # vermelho para 5 V "e 12 V" -- com o 12 V dentro do gabinete, duas
    # tensoes com a mesma cor a 3 cm uma da outra e convite a erro, e o erro
    # destroi Pico, GPS, display e cartao juntos.
    "12V_ENTRADA":   "#ff33cc",  # magenta
    "12V_PROTEGIDO": "#ff33cc",  # magenta
    "5V_CONV":    "#ff1a1a",   # vermelho
    "VSYS_5V":    "#ff1a1a",   # vermelho
    "3V3":        "#418dd9",   # azul
    # BARRAMENTO SPI EM COR ÚNICA, incluindo os dois chip-selects. O
    # barramento é compartilhado entre cartão e display (RNF06), e na
    # perfboard o que se quer enxergar de relance é "isto é SPI" — separar o
    # CS por cor sugeria que ele fosse outro grupo, quando ele é o que
    # distingue quem fala no mesmo barramento.
    "SPI0_SCK":   "#4faf4e", "SPI0_MOSI": "#4faf4e", "SPI0_MISO": "#4faf4e",
    "SD_CS":      "#4faf4e", "TFT_CS":    "#4faf4e",
    # Controle do display, fora do barramento: DC escolhe comando ou dado,
    # RES é reset e BL é o PWM do backlight.
    "TFT_DC":     "#ffe500", "TFT_RST": "#ffe500", "TFT_BL_PWM": "#ffe500",
    "GPS_TX":     "#8c3b00", "GPS_RX": "#8c3b00", "UART_TX_R5": "#8c3b00",
    "ENC_CLK":    "#ff7f00", "ENC_DT": "#ff7f00", "ENC_SW": "#ff7f00",
    "LED_R_GPIO": "#8c00ff", "LED_R_CATODO": "#8c00ff",
    "LED_G_GPIO": "#8c00ff", "LED_G_CATODO": "#8c00ff",
    "LED_B_GPIO": "#8c00ff", "LED_B_CATODO": "#8c00ff",
}
COR_PADRAO = "#999999"

# Posição na vista protoboard. Espaçamento generoso para os fios ficarem
# legíveis; arraste no Fritzing como preferir.
POS_BB = {
    "PICO": (300, 120),
    "GPS": (620, 60), "SD": (620, 150), "TFT": (620, 240), "ENC": (620, 330),
    "J12V": (40, 40), "F1": (120, 40), "TVS1": (180, 100), "C5": (230, 100),
    "CONV": (60, 170), "D1": (170, 60), "C1": (150, 150), "C2": (230, 150),
    "R1": (60, 380), "R2": (60, 420), "R3": (60, 460),
    "LED": (60, 520), "R4": (60, 600), "Q1": (180, 620),
    "BZ": (320, 640), "R5": (430, 60),
}

VISTAS = [("breadboardView", "breadboard"),
          ("schematicView", "schematic"),
          ("pcbView", "copper0")]


def liga(nets):
    """Expande cada rede em pares encadeados: a-b, b-c, c-d..."""
    pares = []
    for nome, pontos in nets.items():
        for i in range(len(pontos) - 1):
            pares.append((pontos[i], pontos[i + 1], nome))
    return pares


def fonte_da_peca_pico():
    """Primeiro arquivo disponível que contenha a peça do Pico."""
    for caminho in FONTES_DA_PECA_PICO:
        if caminho.exists():
            with zipfile.ZipFile(caminho) as z:
                if any("rpi_pico" in n for n in z.namelist()):
                    return caminho
    return None


def main(argv=None):
    p = argparse.ArgumentParser(
        description="Gera o .fzz a partir da tabela de ligações deste arquivo.")
    p.add_argument("--saida", type=Path, default=SAIDA_PADRAO,
                   help=f"arquivo a escrever; padrão {SAIDA_PADRAO.name}")
    p.add_argument("--forcar", action="store_true",
                   help="sobrescreve um arquivo existente; ver o aviso abaixo")
    args = p.parse_args(argv)
    SAIDA = args.saida

    # Proteção do R-29: o .fzz versionado foi editado à mão — roteamento de
    # fios, posicionamento e notas — e regerar por cima DESTRÓI esse trabalho,
    # que não está em nenhum outro lugar. O gerador é a fonte da NETLIST; o
    # .fzz é a fonte do LAYOUT. Para comparar visualmente, gere em outro nome.
    if SAIDA.exists() and not args.forcar:
        print(f"erro: {SAIDA.name} já existe e não será sobrescrito.\n"
              f"\n"
              f"  Esse arquivo pode ter edição manual de layout que o gerador\n"
              f"  não sabe reproduzir: roteamento de fios, posição das peças e\n"
              f"  notas. Regerar por cima apaga tudo isso.\n"
              f"\n"
              f"  Para comparar visualmente, gere com outro nome:\n"
              f"      python3 {Path(__file__).name} --saida coruja_gps_v4.fzz\n"
              f"\n"
              f"  Se tem certeza de que quer descartar o layout atual:\n"
              f"      python3 {Path(__file__).name} --forcar\n",
              file=sys.stderr)
        return 1

    SKETCH_COM_PICO = fonte_da_peca_pico()
    if SKETCH_COM_PICO is None:
        raise SystemExit(
            "erro: preciso da peça do Pico embutida em algum destes:\n"
            + "".join(f"  {c}\n" for c in FONTES_DA_PECA_PICO)
            + "       (part.rpi_pico-tht_1.fzp e seus 3 SVGs)"
        )

    idx = {pid: 100 + i for i, (pid, *_r) in enumerate(PECAS)}
    pares = liga(NETS)

    # ---- offsets reais de cada pino, lidos dos SVG de breadboard ----------
    with zipfile.ZipFile(SKETCH_COM_PICO) as z:
        nomes = z.namelist()
        fzp_pico = next(n for n in nomes if n.endswith("rpi_pico-tht_1.fzp"))
        svg_pico = next(n for n in nomes if "breadboard" in n and "rpi_pico" in n)
        pico_fzp_txt = z.read(fzp_pico).decode("utf-8", "replace")
        pico_svg_txt = z.read(svg_pico).decode("utf-8", "replace")

    off, problemas = {}, []
    for pid, mid, _arq, titulo, _props, _nomes in PECAS:
        kw = {}
        if pid == "PICO":
            kw = dict(svg_embutido=pico_svg_txt, fzp_embutido=pico_fzp_txt)
        o, err = geo.offsets(mid, **kw)
        if o is None:
            problemas.append(f"{titulo}: {err}")
            o = {}
        off[pid] = o
    if problemas:
        print("AVISO — posições não resolvidas (fios podem sair soltos):", file=sys.stderr)
        for p_ in problemas:
            print(f"  {p_}", file=sys.stderr)

    def abs_pino(pid, cid):
        bx, by = POS_BB.get(pid, (100, 100))
        dx, dy = off.get(pid, {}).get(cid, (0.0, 0.0))
        return bx + dx, by + dy

    # ---- conexões: breadboard passa por fio; esquemático/pcb são diretas --
    fios = []            # (modelIndex, cor, x, y, dx, dy, (pa,ca), (pb,cb))
    conn_bb = {}         # (peça, connectorId) -> [(modelIndex_fio, connector_do_fio)]
    conn_dir = {}        # (peça, connectorId) -> [(peça_alvo, connector_alvo)]
    mi_fio = 500
    for (pa, na), (pb, nb), net in pares:
        ca, cb = pino(pa, na), pino(pb, nb)
        conn_dir.setdefault((pa, ca), []).append((pb, cb))
        conn_dir.setdefault((pb, cb), []).append((pa, ca))
        ax, ay = abs_pino(pa, ca)
        bx, by = abs_pino(pb, cb)
        fios.append((mi_fio, CORES.get(net, COR_PADRAO), ax, ay, bx - ax, by - ay,
                     (pa, ca), (pb, cb), net))
        conn_bb.setdefault((pa, ca), []).append((mi_fio, "connector0"))
        conn_bb.setdefault((pb, cb), []).append((mi_fio, "connector1"))
        mi_fio += 1

    L = ['<?xml version="1.0" encoding="UTF-8"?>',
         f'<module fritzingVersion="{FRITZING_VERSION}">',
         '    <views>']
    for v, _lay in VISTAS:
        L.append(f'        <view name="{v}" backgroundColor="#ffffff" '
                 f'gridSize="0.1in" showGrid="1" alignToGrid="1" viewFromBelow="0"/>')
    L += ['    </views>', '    <instances>']

    # ---------------- instâncias de peça ----------------
    for pid, mid, arq, titulo, props, _nomes in PECAS:
        L.append(f'        <instance moduleIdRef="{escape(mid)}" '
                 f'modelIndex="{idx[pid]}" path="{escape(arq)}">')
        for k, val in props.items():
            L.append(f'            <property name="{escape(k)}" value="{escape(val)}"/>')
        L.append(f'            <title>{escape(titulo)}</title>')
        L.append('            <views>')
        for vista, camada in VISTAS:
            if vista == "breadboardView":
                x, y = POS_BB.get(pid, (100, 100))
            else:
                x, y = POS.get(pid, (100, 100))
            L.append(f'                <{vista} layer="{camada}">')
            L.append(f'                    <geometry z="2.5" x="{x:.4f}" y="{y:.4f}"/>')
            meus = sorted({c for (p_, c) in conn_dir if p_ == pid},
                          key=lambda c: int(c[9:]))
            if meus:
                L.append('                    <connectors>')
                for c in meus:
                    L.append(f'                        <connector connectorId="{c}" '
                             f'layer="{camada}">')
                    L.append('                            <connects>')
                    if vista == "breadboardView":
                        for (mfio, cfio) in conn_bb.get((pid, c), []):
                            L.append(f'                                <connect '
                                     f'connectorId="{cfio}" modelIndex="{mfio}" '
                                     f'layer="breadboardWire"/>')
                    else:
                        for (alvo, calvo) in conn_dir[(pid, c)]:
                            L.append(f'                                <connect '
                                     f'connectorId="{calvo}" modelIndex="{idx[alvo]}" '
                                     f'layer="{camada}"/>')
                    L.append('                            </connects>')
                    L.append('                        </connector>')
                L.append('                    </connectors>')
            L.append(f'                </{vista}>')
        L.append('            </views>')
        L.append('        </instance>')

    # ---------------- instâncias de fio (só protoboard) ----------------
    for n, (mfio, cor, x, y, dx, dy, (pa, ca), (pb, cb), net) in enumerate(fios):
        L.append(f'        <instance moduleIdRef="WireModuleID" '
                 f'modelIndex="{mfio}" path="wire.fzp">')
        L.append(f'            <title>{escape(net)}</title>')
        L.append('            <views>')
        L.append('                <breadboardView layer="breadboardWire">')
        L.append(f'                    <geometry z="{3.5 + n * 0.001:.4f}" '
                 f'x="{x:.4f}" y="{y:.4f}" x1="0" y1="0" '
                 f'x2="{dx:.4f}" y2="{dy:.4f}" wireFlags="64"/>')
        L.append(f'                    <wireExtras mils="22.2222" color="{cor}" '
                 f'opacity="1" banded="0"/>')
        L.append('                    <connectors>')
        for cfio, (pt, ct) in (("connector0", (pa, ca)), ("connector1", (pb, cb))):
            L.append(f'                        <connector connectorId="{cfio}" '
                     f'layer="breadboardWire">')
            L.append('                            <geometry x="0" y="0"/>')
            L.append('                            <connects>')
            L.append(f'                                <connect connectorId="{ct}" '
                     f'modelIndex="{idx[pt]}" layer="breadboard"/>')
            L.append('                            </connects>')
            L.append('                        </connector>')
        L.append('                    </connectors>')
        L.append('                </breadboardView>')
        L.append('            </views>')
        L.append('        </instance>')

    L += ['    </instances>', '</module>', '']
    fz = "\n".join(L)

    tmp = AQUI / "_fzz_tmp"
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir()
    (tmp / "coruja_gps.fz").write_text(fz, encoding="utf-8")
    with zipfile.ZipFile(SKETCH_COM_PICO) as z:
        for nome in z.namelist():
            if "rpi_pico" in nome:
                (tmp / os.path.basename(nome)).write_bytes(z.read(nome))
    with zipfile.ZipFile(SAIDA, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(tmp.iterdir()):
            z.write(f, f.name)
    shutil.rmtree(tmp)

    import collections
    porcor = collections.Counter(c for _m, c, *_r in fios)
    print(f"{SAIDA.name}: {len(PECAS)} peças, {len(NETS)} redes, {len(fios)} fios")
    nomes_cor = {"#000000": "preto (GND)", "#ff1a1a": "vermelho (5V)",
                 "#ff33cc": "magenta (12V)",
                 "#418dd9": "azul (3V3)", "#4faf4e": "verde (SPI)",
                 "#ffe500": "amarelo (display)", "#8c3b00": "marrom (GPS/UART)",
                 "#ff7f00": "laranja (encoder)", "#8c00ff": "violeta (LED RGB)",
                 "#999999": "cinza (buzzer)"}
    for cor, n in porcor.most_common():
        print(f"  {n:>3} fios  {nomes_cor.get(cor, cor)}")
    print(f"  {SAIDA.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
