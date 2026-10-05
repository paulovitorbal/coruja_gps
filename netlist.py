"""Netlist do coruja_gps: peças, conectores e redes.

**Fonte única.** Transcrito do `bom_schematic.md`, que é o documento onde a
fiação é revisada por gente. Daqui saem o `.fzz` do Fritzing e o projeto do
KiCad — e é por isso que este módulo existe separado: duas transcrições da
mesma netlist divergiriam, e divergiriam em silêncio.

Quem muda a fiação muda o `bom_schematic.md` primeiro, e depois aqui.
"""
from __future__ import annotations

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
     # GMT024-08-SPI8P ver. 1.3, serigrafia lida no verso em 2026-09-29:
     # ST7789V, 240x320 nativo, usado deitado em 320x240 (rotacao por
     # MADCTL). Ordem dos pinos CONFERIDA na placa fisica pelo autor na
     # mesma data -- bate com a documentada, ao contrario de dois dos tres
     # modulos anteriores (R-34). Ver R-62.
     #
     # VCC vai nos 5 V: o modulo tem LDO proprio (662K / XC6206P332MR).
     # Nao ha MISO -- sao 8 pinos, e nada pode ser lido de volta.
     'Display 2,4" 240x320 ST7789V (GMT024-08-SPI8P)', {},
     ["GND", "VCC 5V", "SCL", "SDA", "RES", "DC", "CS", "BL"]),

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

    # Fusível de 2 A, DENTRO do gabinete (decidido em 2026-09-28). Dimensionado
    # pela corrente do aparelho, não pela capacidade do cabo: quem protege o
    # cabo é o fusível do carro, porque a derivação é no pós-chave e sai da
    # caixa de fusíveis já protegida. Dois fusíveis em série no mesmo ramo
    # seriam redundância sem função.
    #
    # Fica antes do TVS porque o modo de falha desejável de um TVS é curto: se
    # ele grampear e ficar em curto, é este fusível que abre.
    ("F1", "SparkFun-Passives-FUSE-X20MM",
     "sparkfun-passives-fuse-x20mm.fzp",
     "F1 2 A", {}, ["0", "1"]),

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
    # O display entrou aqui em 2026-09-29: o modulo tem LDO proprio (662K /
    # XC6206P332MR, entrada de 1,8 a 6 V, saida de 200 mA), e com 3V3 na
    # entrada esse LDO ficaria EM DROPOUT -- 3,3 V entrando para 3,3 V
    # saindo, sem os 160 mV de folga -- e entregaria ~3,14 V. Nos 5 V ele
    # trabalha em especificacao, e o backlight sai do conversor em vez do
    # trilho de 3V3 do Pico.
    "VSYS_5V":     [("D1", "K"), ("PICO", "39"), ("C1", "+"), ("C2", "0"),
                    ("GPS", "VCC 5V"), ("TFT", "VCC 5V")],

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
    "3V3":         [("PICO", "36"), ("SD", "3V"),
                    ("ENC", "+ 3V3"), ("LED", "K")],

    # SPI0: so o cartao, desde 2026-09-29. O display saiu para o SPI1 e com
    # ele foram o mutex display <-> cartao, a troca de velocidade por
    # transacao (cartao <= 400 kHz na init, display em dezenas de MHz) e o
    # risco de o buffer de nivel do leitor carregar as linhas do display.
    "SPI0_SCK":    [("PICO", "24"), ("SD", "CLK")],
    "SPI0_MOSI":   [("PICO", "25"), ("SD", "CMD")],
    "SPI0_MISO":   [("PICO", "21"), ("SD", "D0")],
    "SD_CS":       [("PICO", "22"), ("SD", "D3")],
    # SEM card detect. O pino DET do modulo fica DESCONECTADO desde
    # 2026-09-22 (ADR 0010): o projeto reage igual a cartao ausente e a cartao
    # ilegivel, e a chave do soquete nao abria por completo -- deixava o GPIO
    # 14 em 1,13 V, na zona indeterminada da logica de 3,3 V.
    #
    # ATENCAO: o GPIO 14 (pino 19) NAO esta mais livre -- virou o TFT_RST
    # abaixo. Se sobrar fiacao do soquete nesse pino, ela disputa o reset do
    # display e mantem o ST7789V em reset permanente.

    # SPI1: display, exclusivo. Seis pinos fisicos em sequencia, 14 a 20, com
    # o GND do pino 18 no meio do bloco -- e a contiguidade e o motivo da
    # mudanca. PICO 14 e 15 sao GP10 e GP11, as funcoes SCK e TX do SPI1 no
    # RP2350; nao sao pinos "livres" quaisquer.
    "SPI1_SCK":    [("PICO", "14"), ("TFT", "SCL")],
    "SPI1_MOSI":   [("PICO", "15"), ("TFT", "SDA")],
    "TFT_CS":      [("PICO", "16"), ("TFT", "CS")],
    "TFT_DC":      [("PICO", "17"), ("TFT", "DC")],
    "TFT_RST":     [("PICO", "19"), ("TFT", "RES")],
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
