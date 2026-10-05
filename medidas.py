"""Medidas dos módulos, a paquímetro. Fonte legível por programa.

O `medidas_modulos.md` guarda o **protocolo** e a narrativa; os números moram
aqui, para o gerador de gabarito e o de footprint lerem o mesmo dado. Duas
cópias dos mesmos números divergiriam em silêncio.

⚠️ **Nada aqui vem de datasheet, foto ou catálogo.** Só o que foi medido na
peça física. O que não foi medido não existe neste arquivo — ausência é
"desconhecido", nunca "provavelmente o valor típico".

Unidade: **milímetro**. O paquímetro do autor é manual e lê em centímetro; a
conversão é feita na hora de anotar, e a primeira leva de 2026-10-05 chegou em
centímetro por engano — por isso a unidade está dita aqui em letras.
"""
from __future__ import annotations

#: Conversor step-down 12 V → 5 V (família LM2596).
#: Medido em 2026-10-05.
CONVERSOR = {
    "titulo": "Conversor step-down 12V->5V",
    # ✅ Contorno confirmado pelo gabarito 1:1 em 2026-10-05.
    "comprimento": 42.9,
    "largura": 21.35,
    "altura": 13.3,            # com o indutor, que é a peça mais alta

    # Conexões: NÃO há barra de pinos. São quatro furos, um em cada quina,
    # para fio. Com espaçador de 2 mm, o fio desce do furo até a ilha do
    # carrier board.
    "tem_pinos": False,
    "conexoes": ("IN+", "IN-", "OUT+", "OUT-"),

    # Furos de fixação, para espaçador M3.
    # ✅ Confirmados contra o gabarito 1:1 em 2026-10-05: os centros bateram.
    "furo_m3_diam": 3.55,
    # Centro do furo, a partir da quina mais próxima. Medido pela BORDA do
    # furo (4,2 mm da borda curta e 0,75 mm da longa) e somado o raio.
    "furo_m3_x": 4.2 + 3.55 / 2,
    "furo_m3_y": 0.75 + 3.55 / 2,

    # ✅ CONFLITO ENCERRADO EM 2026-10-05 — por decisão, não por medida.
    #
    # O módulo fica apoiado em espaçador e é ligado por JUMPER. Fio é flexível:
    # a posição dos furos do módulo deixou de ser carregada pelo desenho da
    # placa, então a medida parou de ser necessária. O requisito sumiu, que é
    # a melhor forma de fechar uma pendência.
    #
    # ⚠️ Isso vale ENQUANTO a ligação for por jumper. Se um dia o módulo for
    # soldado direto na placa, estes números voltam a fazer falta — e nenhum
    # dos dois abaixo serve.
    #
    # Gabarito impresso em 2026-10-05: nenhuma das duas hipóteses caiu nos
    # furos reais.
    #
    # O mesmo gabarito CONFIRMOU o contorno e os dois furos de M3 — o autor
    # alinhou pelas bordas e os círculos caíram no lugar. Logo o erro não está
    # no contorno nem no método: está só na leitura das conexões.
    #
    # O que distingue os dois casos é COMO foi medido. O M3 saiu de "borda do
    # furo até borda da placa, mais o raio", e acertou. As conexões saíram de
    # vão entre furos e de uma distância à borda cujo ponto de referência
    # (borda ou centro do furo) não ficou registrado — e é provavelmente aí
    # que mora a diferença.
    #
    # Lição para os próximos módulos: anotar SEMPRE de que ponto do furo a
    # distância foi tomada. Sem isso a medida não é reprodutível.
    #
    # As conexões foram medidas por dois caminhos que deviam concordar e não
    # concordam. A diferença de 0,70 mm no comprimento é maior que o erro
    # esperado de leitura, e num footprint é a diferença entre entrar e não
    # entrar — por isso não se tira média.
    #
    # A: pelo vão entre furos        19,0 × 41,0 mm
    # B: pela distância à borda      18,75 × 40,30 mm  (21,35-2x1,3 e 42,9-2x1,3)
    #
    # Contra a hipótese A: ela põe o centro do furo a 0,95 mm da borda, e furo
    # nenhum cabe aí sem romper a borda. Mas isso é raciocínio, não medida —
    # quem decide é a peça sobre o papel.
    # Ambas descartadas pelo gabarito; ficam como registro do que NÃO é.
    "conexao_vao_a": (41.0, 19.0),
    "conexao_vao_b": (40.30, 18.75),
    "conexao_confirmada": "nao se aplica: ligacao por jumper",
    "conexao_diam": None,      # nao medido, e nao mais necessario

    "espacador_altura": 2.0,   # escolha do autor, não medida

    # Deixou de importar junto com o resto: jumper liga de qualquer lado.
    "lado_dos_furos": None,
}


#: Leitor microSD — Adafruit 4682 ("Micro SD SPI or SDIO Card Breakout - 3V").
#:
#: ⚠️ **Estes números NÃO foram medidos a paquímetro.** Saem do arquivo de
#: placa em EAGLE que a própria Adafruit publica, que é fonte melhor que
#: medida e melhor que catálogo — é o desenho de onde a peça foi fabricada:
#:
#:   github.com/adafruit/Adafruit-MicroSD-SPI-or-SDIO-card-breakout-PCB
#:   Projeto de Limor Fried/Ladyada para a Adafruit Industries.
#:   Creative Commons Attribution/Share-Alike. A atribuição acima é condição
#:   da licença e tem de acompanhar qualquer redistribuição.
#:
#: Confirmação independente: a ordem dos nove sinais no conector bate pad a
#: pad com a do `netlist.py`, que foi transcrita do `bom_schematic.md` sem
#: conhecer este arquivo.
#:
#: ✅ CONFIRMADO contra a peça física com o gabarito 1:1 em 2026-10-05: os nove
#: pinos e os dois furos de fixação bateram, e os rótulos caíram sobre os nomes
#: da serigrafia — o que descarta folha espelhada.
#:
#: Única divergência: a peça tem as quinas ARREDONDADAS, e o arquivo as desenha
#: CHANFRADAS. Não muda pino nem furo. Muda a área de ocupação, porque o
#: arredondamento avança ~0,74 mm além do chanfro no meio da quina — por isso o
#: footprint usa o retângulo envolvente ali, que limita os dois casos.
LEITOR_SD = {
    "titulo": "Leitor microSD Adafruit 4682",
    "fonte": "arquivo EAGLE oficial da Adafruit (nao medido)",
    "comprimento": 25.4,
    "largura": 22.86,
    "altura": 3.5,             # catalogo; o conector de barra soma por baixo
    "tem_pinos": True,

    # Contorno com as quatro quinas chanfradas a 45 graus, 2,54 mm.
    "contorno": [(25.4, 20.32), (25.4, 2.54), (22.86, 0.0), (2.54, 0.0),
                 (0.0, 2.54), (0.0, 20.32), (2.54, 22.86), (22.86, 22.86)],

    # Furos de fixação: M2.5 metalizados, os dois na borda OPOSTA ao conector.
    "furo_diam": 2.5,
    "furos": [(2.54, 20.32), (22.86, 20.32)],

    # Barra de 9 pinos, passo 2,54, furo 1,0 mm, ao longo de y=2,54.
    # Os nomes sao os da SERIGRAFIA DA ADAFRUIT, que e o que esta escrito na
    # peca — e portanto o que da para conferir olhando. O nome equivalente no
    # projeto esta entre parenteses no documento do gabarito.
    "passo": 2.54,
    "pino_furo": 1.0,
    "pinos": [(2.54 + i * 2.54, 2.54, nome) for i, nome in enumerate(
        ["3.3V", "GND", "SCLK", "DO", "DI", "CS", "DAT1", "DAT2", "CARDDET"])],

    # O soquete do cartao fica do lado OPOSTO ao conector, e a boca aponta
    # para fora dessa borda. Importa para a montagem: o cartao precisa entrar
    # e sair, entao essa borda nao pode ficar contra a parede da caixa.
    "boca_do_cartao": "borda oposta ao conector de 9 pinos",
}


#: GPS GY-GPS6MV2.
#:
#: ℹ️ **NÃO É ERRO** a placa ser GY-GPS6MV2 e o chip ser NEO-M8N.
#:
#: Essa placa é vendida com vários chips u-blox soldados — do NEO-6M ao M8N —
#: e o autor escolheu a versão **M8N** na compra, em 2026. O nome da placa vem
#: da família, não do chip.
#:
#: Fica dito aqui porque a combinação parece contraditória para quem conhece
#: a peça: GY-GPS6MV2 é historicamente a placa do NEO-6M. Sem este registro,
#: a próxima pessoa "corrige" para NEO-6M e introduz um erro — e a diferença
#: é funcional: 5 Hz contra 10 Hz de taxa máxima, e só GPS contra multi-GNSS.
#:
#: Consequência prática: os 4 Hz do projeto ficam com folga confortável, e o
#: monitor de taxa (RF01.5) mede algo longe do teto do módulo.
#:
#: ✅ **CONFIRMADO NA PEÇA** em 2026-10-05, por foto da própria placa: a lata
#: do u-blox traz `NEO-M8N-0-10`, lote `2248` (semana 48 de 2022), e a
#: serigrafia da placa traz `GY-GPS6MV2`. As duas inscrições na mesma imagem.
#: Não é memória de compra nem catálogo — é a peça.
#:
#:
#: ⚠️ **HIPÓTESE NÃO CONFIRMADA.** Nada aqui foi medido nem veio do fabricante.
#: Saiu de um footprint de terceiro, de 2015, achado por indicação do autor:
#:
#:   github.com/timelab/ADEM  —  libraries/ADEM/GY-NEO6MV2.kicad_mod
#:
#: O que nele é **confiável**: o passo de 2,54 (o vão pino 1→4 dá 7,62 exato)
#: e a ordem VCC/RX/TX/GND, que casa pad a pad com o `netlist.py`.
#:
#: 🔴 O que nele é **impossível**: os furos de fixação de ⌀4,0 mm, com centro a
#: 2,2 mm das duas bordas. Sobrariam 0,2 mm de material até a borda — não se
#: fabrica assim. Estão aqui para o gabarito mostrar o absurdo, não para virar
#: footprint.
#:
#: 🟡 O que nele é **suspeito**: a posição absoluta. O primeiro pino a 9,43 mm
#: da borda não é número redondo nem em mm nem em polegada (0,371"). Compare
#: com o arquivo da Adafruit, onde tudo caía em múltiplo de 2,54.
#:
#: ⚪ O que nele **falta**: o conector U.FL, que para este projeto é a posição
#: mais consequente do módulo — o rabicho tem 8 cm (R-67).
#: Medidas tomadas da BORDA DO FURO até a borda da placa — é o que o
#: paquímetro manual alcança com confiança. O centro sai somando o raio.
_GPS_C, _GPS_L = 36.4, 26.0          # comprimento (borda longa) e largura
#: ⚠️ Corrigido em 2026-10-05 com paquímetro DIGITAL. A leitura anterior, de
#: paquímetro manual, dava 4,8 mm — quase certamente o anel de cobre em volta,
#: não o furo. A diferença não é de precisão, é de peça: 2,7 mm é passagem de
#: **M2.5**, e 4,8 seria M4. Espaçador comprado pelo número velho não entraria.
_GPS_FURO_D = 2.7                    # diametro interno, paquimetro digital
_GPS_FURO_BORDA = 1.0                # borda do furo até cada borda da placa
_GPS_PINO_D = 1.0                    # MEDIDO no digital (era presumido)
_GPS_VCC_LONGA = 8.8                 # borda do furo do VCC até a borda longa
_GPS_VCC_CURTA = 1.2                 # idem, até a borda curta (digital)

#: ⚠️ As duas acima são da BORDA do furo, não do centro — é como as demais
#: foram tomadas, e o autor não refez pela técnica de dois lados por achar o
#: erro humano maior que o ganho, num furo de 1 mm. Concordo.
#:
#: A interpretação "borda" também é a única que fecha com fabricação: se 1,2
#: fosse o CENTRO, a ilha de cobre de ~1,7 mm do furo chegaria a 0,35 mm da
#: borda da placa, o que não se faz. Com 1,2 na borda, o centro cai em 1,7 e
#: sobra 0,85 mm de material — valor normal.

#: ⚠️ As posições abaixo são DERIVADAS, nunca digitadas. Em 2026-10-05 as
#: dimensões da placa foram corrigidas de 35 x 25 para 36,4 x 26, e as posições
#: estavam gravadas em absoluto — toda a geometria passou a mentir de uma vez.
#: Derivar da borda faz uma correção de dimensão se propagar sozinha.
_GPS_FC = _GPS_FURO_BORDA + _GPS_FURO_D / 2      # centro do furo à borda
_GPS_PX = _GPS_C - (_GPS_VCC_CURTA + _GPS_PINO_D / 2)
_GPS_PY = _GPS_VCC_LONGA + _GPS_PINO_D / 2

GPS = {
    "titulo": "GPS GY-GPS6MV2 (u-blox NEO-M8N)",
    "fonte": "medido a paquimetro em 2026-10-05, da borda de cada furo",
    "comprimento": _GPS_C,
    "largura": _GPS_L,
    #: ⚠️ 3,0 mm é a altura MEDIDA SOBRE O CHIP M8N, não necessariamente o
    #: ponto mais alto da placa — a foto mostra pelo menos um componente
    #: cilíndrico que parece mais alto. Para folga dentro da caixa vale o
    #: ponto mais alto, então este número ainda não serve para esse cálculo.
    "altura": 3.0,
    "altura_referencia": "topo do chip u-blox, nao o ponto mais alto",
    #: Conector U.FL sozinho, acima do plano da placa. O plugue do rabicho
    #: soma por cima — e é a soma que decide a folga sob a prateleira.
    "ufl_altura": 1.8,
    "tem_pinos": True,
    "montagem": "espacador + jumper",

    "furo_diam": _GPS_FURO_D,
    "furos": [(_GPS_FC, _GPS_FC), (_GPS_C - _GPS_FC, _GPS_FC),
              (_GPS_FC, _GPS_L - _GPS_FC), (_GPS_C - _GPS_FC, _GPS_L - _GPS_FC)],

    "passo": 2.54,
    "pino_furo": _GPS_PINO_D,
    "pinos": [(_GPS_PX, _GPS_PY + i * 2.54, nome)
              for i, nome in enumerate(["VCC", "RX", "TX", "GND"])],

    #: U.FL — a única medida tomada do CENTRO, dito pelo autor. Fica na borda
    #: oposta à barra de pinos, junto a um canto: o rabicho de 8 cm sai pelo
    #: lado contrário ao do chicote de sinal, e os dois não disputam espaço.
    #: Ver `montagem.md` e R-67. Ainda NÃO conferido contra a peça.
    #: Centro = média de duas leituras por eixo, no paquímetro digital:
    #:   borda curta  2,4 e 4,1  ->  centro 3,25
    #:   borda longa  6,5 e 8,1  ->  centro 7,30
    #: Bate com a leitura manual anterior (3,7 ; 7,45) dentro de 0,45 e 0,15.
    #:
    #: ⚠️ A diferença dos pares dá o corpo medido: 1,7 x 1,6 mm. Eu esperava
    #: ~2,6 mm, que é o corpo de um U.FL/MHF1 — então **a conferência de
    #: identidade do conector NÃO fechou**. A explicação provável é que a garra
    #: pegou o anel central e não o corpo inteiro, mas isso é suposição. Fica
    #: registrado como não confirmado; a posição, essa sim, está boa.
    "ufl": (3.25, 7.30),
    "ufl_corpo_medido": (1.7, 1.6),

    #: Há um quinto furo ao lado do conector da antena. Não medido, por decisão
    #: do autor: não é de espaçador e não entra na fixação.
    "quinto_furo": None,
}


#: Antena GPS: patch cerâmico de 25 x 25 mm, com rabicho de 8 cm e plugue U.FL.
#:
#: 📌 **Decisão de 2026-10-05: a antena vai DIRETO NA PCB, colada com fita
#: espuma dupla face.** Isso aposenta a prateleira de fibra presa à tampa, que
#: o `montagem.md` descreve — segundo o autor, aquele arranjo ficou bagunçado e
#: dificultava fechar a caixa.
#:
#: A mudança melhora três coisas de uma vez:
#:  - o rabicho de 8 cm deixa de ser restrição, porque módulo e antena ficam
#:    vizinhos na mesma placa (era a limitação mais dura do projeto, R-67);
#:  - a antena ganha PLANO DE TERRA sob si, que é o que um patch quer e que a
#:    prateleira de fibra não oferecia;
#:  - a antena fica na extremidade oposta ao conversor chaveado, longe da fonte
#:    de ruído de verdade — que é o chaveamento e o indutor do LM2596, não os
#:    capacitores.
#:
#: ⚠️ Duas regras que isso impõe ao desenho da placa:
#:  1. ÁREA PROIBIDA sob a antena: plano de terra sólido, sem trilha passando;
#:  2. NADA ACIMA dela. O patch enxerga o céu pela tampa, que é plástica e
#:     portanto transparente a 1,5 GHz. Qualquer peça por cima anula isso.
#:
#: ⚠️ A fita precisa ser acrílica estruturada (tipo VHB), especificada para
#: 90-120 °C. O painel mediu 92 °C (R-65); fita espuma comum não descola, ela
#: escorre devagar, e a peça desce.
ANTENA = {
    "titulo": "Antena patch ceramica GPS 25x25",
    "fonte": "medido a paquimetro digital em 2026-10-05",
    "comprimento": 25.0,
    "largura": 25.0,
    "altura": None,            # nao medido
    "tem_pinos": False,
    "montagem": "fita espuma dupla face, direto na PCB",
    "rabicho_mm": 80.0,
}


#: Display 2,4" ST7789V. Medido a paquímetro digital em 2026-10-05.
#:
#: 📌 Fixado na FACE FRONTAL da caixa, não na PCB. Na placa ele é apenas um
#: conector de 8 vias para o chicote — não ocupa área nem contorno.
#:
#: ⚠️ Mas ocupa VOLUME: o módulo de 70 x 46,7 mm fica pendurado atrás da face
#: frontal, invadindo ~6 mm para dentro. O que estiver na borda frontal da PCB
#: tem de ser baixo, ou não estar ali.
DISPLAY = {
    "titulo": "Display 2,4 ST7789V",
    "fonte": "medido a paquimetro digital em 2026-10-05",
    # A placa inteira do módulo.
    "comprimento": 70.0,
    "largura": 46.7,
    "altura": 5.7,                 # no ponto mais alto
    # O vidro mais a moldura preta — é esta medida que vira o RECORTE da face.
    "visivel": (60.8, 44.0),
    "visivel_altura": 3.2,
    "tem_pinos": True,
    "montagem": "face frontal da caixa; na PCB e so conector",
}

#: Encoder rotativo KY-040. Medido a paquímetro digital em 2026-10-05.
#:
#: 📌 Também na face frontal, ao lado do display.
#:
#: O eixo tem 20,0 mm de comprimento LIVRE, acima do corpo. Descontando 3 mm
#: de parede e ~2 mm de arruela e porca, restam **15 mm** para o knob agarrar —
#: folga confortável.
ENCODER = {
    "titulo": "Encoder rotativo KY-040",
    "fonte": "medido a paquimetro digital em 2026-10-05",
    "comprimento": 19.3,
    "largura": 26.5,
    "altura": 9.6,                 # sem o eixo
    "eixo_livre": 20.0,            # comprimento do eixo acima do corpo
    "tem_pinos": True,
    "montagem": "face frontal da caixa, a ESQUERDA do display",
}

#: 📌 Decisões de montagem de 2026-10-05, que amarram o desenho da placa:
#:
#:  - LEITOR microSD e PICO ficam FACEADOS com a borda, cada um com seu vão na
#:    caixa: um para trocar o cartão, outro para o cabo de atualização do
#:    firmware. Isso FIXA a orientação dos dois: a boca do cartão e o conector
#:    USB têm de apontar para fora.
#:  - LED: furo único e centralizado, saindo para a face de CIMA.
#:  - Caixa: impressa em 3D, o que aposenta a patola e a prateleira.
MONTAGEM = {
    "faceados": ("leitor_sd", "pico"),
    "vaos_cartao_e_usb": "face TRASEIRA (fundo)",
    "led": "face de cima, furo unico centralizado",
    "borda_abaixo_do_display": 10.0,
    "caixa": "impressa em 3D",

    #: 📌 A caixa é presa ao painel por ADESIVO MAGNÉTICO. Para atualizar o
    #: firmware, desliga-se a alimentação e leva-se o conjunto inteiro.
    #:
    #: ⚠️ Duas consequências térmicas, no painel de 92 °C medido (R-65):
    #:   - a retenção magnética CAI com a temperatura: ferrite flexível perde
    #:     fluxo de forma sensível na faixa de 80-100 °C;
    #:   - o adesivo do magnético sofre o mesmo escorrimento lento da fita
    #:     espuma, e pela mesma razão.
    #: Um aparelho que se solta do painel em movimento é problema de segurança,
    #: não de acabamento. Conferir retenção com a caixa quente antes de confiar.
    "fixacao_ao_painel": "adesivo magnetico",
}

#: Buzzer SFM-27. Medido a paquímetro digital em 2026-10-05.
#: Fica na face frontal, à DIREITA do display. Na PCB continua sendo só um
#: conector de 2 vias — vai por fio, como o LED.
#:
#: ✅ Vão entre os furos MEDIDO em 2026-10-05, pela técnica de dois lados:
#: borda interna a borda interna = 27,0 mm, mais um diâmetro de furo = 29,6 mm
#: entre centros.
#:
#: ⚠️ Isso NÃO reconcilia com o "0,8 mm da borda do furo até a borda da orelha"
#: anotado antes, que dava 30,4 mm. Com 29,6 entre centros e orelha a orelha de
#: 34,6, sobram 1,2 mm de material além do furo, não 0,8. A medida direta vale
#: mais que a cadeia derivada, então o 0,8 fica descartado — provavelmente foi
#: tomado de outro ponto.
BUZZER = {
    "titulo": "Buzzer SFM-27",
    "fonte": "medido a paquimetro digital em 2026-10-05",
    "diametro": 22.4,
    "orelha_a_orelha": 34.6,
    "furo_diam": 2.6,
    "furo_borda_a_borda_interna": 27.0,            # medido
    "vao_entre_furos": 27.0 + 2.6,                 # = centro a centro
    #: 📌 ORELHAS NA VERTICAL — decisão, não medida, registrada em 2026-10-05.
    #:
    #: A coluna do buzzer fica 22,4 mm de largura por 34,6 de altura. Deitadas,
    #: seriam 34,6 x 22,4 e a face frontal passaria de 117,7 para 129,9 mm,
    #: **sem ganhar nada em altura** — o display, com 46,7 mais 10 de borda,
    #: domina os 56,7 mm nos dois casos. Girar o buzzer só empurra a largura.
    "orelhas": "vertical",
    "montagem": "face frontal, a DIREITA do display",
}

MODULOS = {"conversor": CONVERSOR, "leitor_sd": LEITOR_SD, "gps": GPS}
