# 🗺️ Projeto: Detector de Radares GPS Inteligente (Pico 2 W)

**Plataforma:** Raspberry Pi Pico 2 W (RP2350) — firmware em **C++17 / Pico SDK**
**Revisão:** 2 — 2026-09-15

> **Revisão 2:** incorpora as correções de `revisao_tecnica.md`. Alterações em relação à
> revisão 1 destacadas com ⚠️ e rastreadas ao final. Não há suposições pendentes neste
> documento; restam **3 medições de bancada** e **1 inspeção** (regulador da placa GPS),
> listadas ao final.

---

## 📦 Bill of Materials (BOM) — Lista Completa de Componentes

| Item | Componente | Especificação Técnica / Detalhes | Função no Projeto |
| :---: | :--- | :--- | :--- |
| 1 | **Raspberry Pi Pico 2 W** | Microcontrolador Dual-Core RP2350 com Wi-Fi (pinos macho pré-soldados) | Cérebro do sistema, processamento NMEA e conectividade sem fio. |
| 2 | **Módulo GPS u-blox NEO-M8N** | Conector serial UART com antena ativa externa SMA | Rastrear velocidade, coordenadas e rumo em tempo real. |
| 3 | **Leitor Micro SD Adafruit 4682** | Breakout board nativo para nível lógico de 3,3 V (*3V ONLY!*) | Interface física para o cartão de memória. |
| 4 | **Cartão Micro SD** | 8 GB ou 16 GB, formatado em **FAT32** | Armazenar `radares.bin` (214 KB) e `wifi.cfg`. |
| 5 | **Display IPS TFT 1.3" ST7789** | Colorido, 240×240 pixels, interface SPI com pino de Backlight (BL) | Exibir velocidade, limites e alertas visuais. |
| 6 | **Encoder Rotativo KY-040** | Módulo incremental com chave/botão de pressão no eixo | Girar: ajuste PWM do brilho.<br>Clicar: comando de atualização Wi-Fi. |
| 7 | **Buzzer Ativo SFM-27** | Piezoelétrico de alta potência (95 dB a 105 dB), 5 V | Bipes estridentes audíveis no painel do veículo. |
| 8 | **LED RGB 5mm Difuso** | **Cátodo comum** (terminal mais longo vai ao GND) | **Único indicador luminoso do projeto.** Estado de via: verde / amarelo / rosa / vermelho. |
| 9 | ⚠️ **Transistor NPN BC337** | TO-92 — ou **2N2222**. **Não usar BC547** | Chave eletrônica para acionar o buzzer de 5 V com margem de corrente. |
| 10 | **Resistor de 330 Ω** | 1 unidade | Canal **vermelho** do LED RGB. |
| 11 | ⚠️ **Resistores de 68 Ω** | 2 unidades (ou 47–100 Ω) | Canais **verde e azul** do LED RGB — ver nota crítica abaixo. |
| 12 | ⚠️ **Resistores de 1 kΩ** | **2 unidades** — filme de carbono ou metálico | Base do transistor do buzzer **+ série no `GPIO 0 → GPS RX`** (R-22). |
| 13 | ⚠️ **Diodo Schottky 1N5819** | Ou SS34 / 1N5817 — queda direta ≤ 0,45 V | **Novo na rev. 2.** Proteção da entrada de 5 V em `VSYS`. |
| 14 | **Diodo 1N4148** | Comutação rápida | Proteção opcional no conector do buzzer — ver nota. |
| 15 | ⚠️ **Capacitor Eletrolítico** | **470 µF a 1000 µF / 16 V (ou 25 V), 105 °C** — atenção à polaridade | Filtrar quedas de tensão e ruído de baixa frequência do alternador. |
| 16 | **Capacitor Cerâmico** | **100 nF (0,1 µF)** — código impresso: 104 | Suprimir ruído de alta frequência da ignição. |
| 17 | ⚠️ **Capacitores Cerâmicos 100 nF** | 2 unidades — código 104 | **Novo na rev. 2.** Debounce em hardware do encoder (CLK e DT). |
| 18 | ⚠️ **Conector JST-XH 2 vias** | Par macho + fêmea, com cabo | **Substitui o Jack P2.** Saída do buzzer, polarizada e sem contato deslizante. |
| 19 | **Placa Perfurada (Perfboard)** | Fenolite ou fibra com furos metalizados (pitch 2,54 mm) | Base de montagem do circuito. |
| 20 | **Barras de Pinos Fêmea 1x20** | Duas fileiras, espaçamento 2,54 mm | Soquete para encaixar e remover o Pico 2 W sem soldá-lo direto. |

### ⚠️ Nota crítica — resistores do LED RGB (itens 11 e 12)

LEDs difusos verde e azul têm tensão direta típica de **3,0 a 3,2 V**. Com o GPIO em
3,3 V e 330 Ω em série, sobram ~0,1–0,3 V no resistor, ou seja **menos de 1 mA** —
emissão praticamente invisível. O vermelho (Vf ≈ 2,0 V) fica em ~4 mA e funciona.

Usar 330 Ω nos três canais, como previsto na revisão 1, deixaria **dois dos quatro
estados de alerta ilegíveis**. Consequência direta: a **Zona de Semáforo** (RF03.3) usa
alternância **amarelo/vermelho**, e amarelo é mistura de vermelho + verde — sem o canal
verde a alternância apareceria como vermelho constante, indistinguível da Zona de
Perigo. Este item é **pré-requisito funcional** daquele requisito, não ajuste estético.

**Medir Vf e corrente reais** dos LEDs adquiridos e ajustar, depois equalizando as
cores por PWM em software.

### ⚠️ Nota — transistor do buzzer (item 10)

O BC547 tem corrente de coletor máxima de 100 mA. Um piezo de 95–105 dB pode consumir
30–50 mA, mais a capacitância do cabo até o painel — margem insuficiente. O BC337
(500 mA) e o 2N2222 (800 mA) têm o mesmo custo e encapsulamento. **Confirmar a pinagem
no datasheet**, que difere entre modelos. Medir o consumo real do buzzer adquirido.

### ⚠️ Nota — diodo do buzzer (item 15)

A revisão 1 justificava este diodo como "roda livre para absorver o pico de retorno
magnético do buzzer". **A justificativa estava incorreta:** o SFM-27 é piezoelétrico
ativo, uma carga capacitiva com oscilador interno, sem indutância significativa. Não há
pico de retorno magnético a absorver.

O componente pode ser mantido (é inofensivo e protege caso o buzzer seja trocado por um
modelo eletromagnético no futuro), mas **não** deve ser considerado proteção necessária.
Se mantido, a orientação está correta: catodo no 5 V, anodo no coletor.

---

## 🔌 Esquema de Conexões e Roteamento (Fritzing)

Mapeamento de nós para interligar os componentes na aba "Esquemático" ou "Protoboard".

### ⚠️ 1. Barramento de Entrada e Filtro Duplo de Energia

> **Correção da revisão 1:** o documento anterior mandava a entrada para "VBUS (Pino
> 39)". No pinout real do Pico 2, **pino 39 = `VSYS`** e **pino 40 = `VBUS`** — o rótulo
> estava trocado. Além disso, `VBUS` é ligado diretamente ao conector USB: injetar 5 V
> externo ali cria conflito de fontes quando o USB é usado para gravação ou debug. A
> entrada correta é `VSYS`, através de diodo Schottky.

* **Carregador USB Veicular (+5 V)** → **Anodo** do **Diodo Schottky 1N5819**.
* **Catodo do Schottky** (lado da faixa) → pino **`VSYS` (Pino 39)** do Pico 2 W.
* **Carregador USB Veicular (GND)** → pino **`GND` (Pino 38)** do Pico 2 W.
* **Capacitor Eletrolítico (470 µF a 1000 µF, 105 °C):** terminal positivo `(+)` na
  linha `VSYS (39)`, terminal negativo `(−)` (lado da faixa cinza) na linha `GND (38)`.
* **Capacitor Cerâmico (100 nF):** um terminal em `VSYS (39)`, o outro em `GND (38)`.

> *Nota de montagem:* monte o par de capacitores em paralelo elétrico e o mais próximo
> fisicamente possível dos pinos 39 e 38. O Schottky fica antes deles, entre o
> carregador e o nó filtrado.
>
> *Nota:* o Schottky isola a fonte externa e evita retorno de corrente, ao custo de
> ~0,3–0,45 V de queda. O `VSYS` do Pico aceita 1,8 V a 5,5 V, então os ~4,6 V
> resultantes estão bem dentro da faixa.

### 2. Barramento SPI0 (Compartilhado: Display ST7789 + SD Adafruit)

* **Pico GPIO 18 (Pino 24 / CLK):** conecta ao pino **`CLK`** do leitor SD **E** ao pino
  **SCL** do Display TFT.
* **Pico GPIO 19 (Pino 25 / MOSI):** conecta ao pino **`CMD/SI`** do leitor SD **E** ao
  pino **SDA** do Display TFT.
* **Pico GPIO 16 (Pino 21 / MISO):** conecta **apenas** ao pino **`DO/SO`** do leitor SD.
* **Pico GPIO 17 (Pino 22 / SD_CS):** conecta ao pino **`D3/CS`** do leitor SD.
* ⚠️ **Pico GPIO 14 (Pino 19 / SD_DET):** conecta ao pino **`DET`** do leitor SD.
  **Novo na rev. 3** — ver nota de card detect abaixo.
* **Pico GPIO 20 (Pino 26 / TFT_CS):** conecta ao pino **CS** do Display TFT.
* **Pico GPIO 21 (Pino 27 / TFT_DC):** conecta ao pino **DC/RS** do Display TFT.
* **Pico GPIO 22 (Pino 29 / TFT_RST):** conecta ao pino **RES/RESET** do Display TFT.
* **Pico GPIO 15 (Pino 20 / PWM BL):** conecta ao pino **BL / BLK** (Backlight).
* **Alimentação do Bloco:** conecte o pino **`3V`** do leitor SD **E** o **VCC** da tela
  TFT no pino **`3V3_OUT` (Pino 36)**. Conecte os **GND** de ambos ao GND comum.

#### ⚠️ Pinagem física do leitor microSD (conferida na placa, 2026-09-17)

O leitor tem **8 pinos**, não 6, e a serigrafia usa nomes duplos (modo SD / modo SPI).
Da esquerda para a direita, olhando de frente:

| # | Serigrafia | Liga em | Função em modo SPI |
| :---: | :--- | :--- | :--- |
| 1 | `3V` | `3V3_OUT` (pino 36) | alimentação |
| 2 | `GND` | GND comum | terra |
| 3 | `CLK` | GPIO 18 (pino 24) | clock do SPI0 |
| 4 | `DO/SO` | GPIO 16 (pino 21) | saída do cartão → MISO |
| 5 | `CMD/SI` | GPIO 19 (pino 25) | entrada do cartão → MOSI |
| 6 | `D3/CS` | GPIO 17 (pino 22) | chip select |
| 7 | `DAT2` | **desconectado** | não usado em modo SPI |
| 8 | `DET` | GPIO 14 (pino 19) | card detect — ver abaixo |

> ⚠️ **Use os rótulos da serigrafia, não nomes genéricos.** A revisão 2 deste documento
> descrevia `VCC/GND/CLK/DI/DO/CS`, que eram inferência e não conferiam com a placa.

#### 🆕 Card detect (`DET` → GPIO 14)

O `DET` permite ao firmware distinguir **"cartão ausente"** — que o motorista resolve
inserindo o cartão — de **"cartão ilegível"**, que ele não resolve. Sem isso, o RF07
trata os dois como a mesma falha.

**Polaridade e pull-up — documentados pelo fabricante** (guia da Adafruit para este
breakout, `adafruit-microsd-spi-sdio.pdf`, pág. 8):

> *"DET — Detect whether a microSD card is inserted. This pin is connected to GND
> internally when there's no card, but when one is inserted it is pulled up to 3V with
> a 4.7 kΩ resistor. That means that when the pin's logic level is False there's no
> card and when it's True there is."*

| Estado | Nível em GPIO 14 |
| :--- | :--- |
| **Cartão inserido** | **ALTO** — 3 V através do pull-up de 4,7 kΩ da placa |
| **Sem cartão** | **BAIXO** — ligado ao GND internamente |

* ⚠️ **Não habilitar o pull-up interno do Pico.** A placa já traz 4,7 kΩ para 3 V.
  Configure o GPIO 14 como entrada simples, sem pull. O interno (~50–80 kΩ) seria
  redundante e mais fraco que o da placa.
* **Nenhum componente novo.**
* **GPIO 14 estava livre** desde que o LED verde de Wi-Fi saiu do projeto (R-25).

> 📄 O mesmo guia confirma que **há pull-up em todos os pinos de lógica SPI** da placa
> (`CLK`, `SO`, `SI`, `CS`), então não são necessários pull-ups externos. E reforça que
> o pino `3V` aceita **somente 3,3 V** — 5 V danifica o cartão.

### ⚠️ 3. Módulo GPS NEO-M8N (UART0)

* **GPS TX** → **Pico GPIO 1 (Pino 2 / RX)**.
* **GPS RX** → **Pico GPIO 0 (Pino 1 / TX)**.
* **GPS VCC** → **depende do regulador da placa breakout.** Ver árvore de decisão abaixo.
* **GPS GND** → GND comum.

#### ⚠️ Alimentação: a faixa do chip não é a faixa da placa

**Placa adquirida: GY-GPSV3-NEO M8N** (breakout estilo Arduino, informado pelo autor).

O datasheet oficial do CI u-blox (UBX-15031086, Tabela 10) especifica:

| Parâmetro | Min | Típico | Máx | Máx. absoluto |
| :--- | :---: | :---: | :---: | :---: |
| `VCC` NEO-M8N | **2,7 V** | **3,0 V** | **3,6 V** | 3,6 V |

**Esse é o limite do CI, não da placa.** A GY-GPSV3, como toda a família GY-*, traz
**regulador LDO embarcado** justamente para aceitar os 5 V do ecossistema Arduino — é a
razão de existir do breakout.

#### ✅ Decisão: alimentar em 5 V

* **GPS VCC** → linha de **5 V** (nó `VSYS` filtrado, após o Schottky).

Três motivos:

1. **É a configuração para a qual a placa foi projetada.** Sem risco de dropout.
2. **Funciona com qualquer LDO que a placa traga.** Alimentar em 3,3 V só funciona se o
   regulador for de baixa queda; se for um AMS1117 (queda de ~1,1–1,3 V), a saída cai a
   ~2,1 V — **abaixo do mínimo de 2,7 V** — e o GPS não fixa ou fixa de forma
   intermitente. O sintoma se confunde com antena ruim, não com erro elétrico.
3. **Alivia o regulador interno do Pico em ~82 mA** no ponto mais apertado do orçamento
   (ver R-13 e a seção abaixo). A carga passa para o LDO da placa GPS.

#### ⚠️ Uma verificação obrigatória antes de ligar os 5 V

> ⚠️ **Confirme visualmente que a placa tem regulador.** Procure o encapsulamento de
> 3 pinos (SOT-23, SOT-89 ou SOT-223) próximo ao pino VCC. **Se não houver**, o VCC vai
> direto ao CI e os 5 V excedem o máximo absoluto de 3,6 V, **destruindo o módulo**.
>
> A ausência de regulador é improvável nesta família de placas, mas o custo de conferir
> é de 30 segundos e o custo de errar é o módulo.
>
> *Teste alternativo, se a marcação estiver ilegível:* alimente a placa com 5 V **na
> bancada, sem o Pico conectado** e meça a tensão no pino VCC do CI u-blox (ou num test
> point de 3,3 V, se exposto). ~3,3 V confirma o regulador; ~5 V significa desligar
> imediatamente.

#### ⚠️ Resistor em série obrigatório no `GPIO 0 → GPS RX`

* **Pico GPIO 0 (Pino 1 / TX)** → **Resistor de 1 kΩ** → **GPS RX**.

O datasheet especifica faixa de operação de entrada `VIN` de **0 a VCC** (Tabela 10),
com máximo absoluto de `VCC + 0,5 V` quando VCC < 3,1 V (Tabela 9). O Pico aciona o GPIO
em **3,3 V**; se o LDO da placa entregar 3,0 V (o típico do CI), os 3,3 V excedem a faixa
de operação em 0,3 V, com apenas 0,2 V até o limite absoluto.

O resistor de 1 kΩ limita qualquer corrente de clamp a menos de 1 mA, contra o `IPIN`
máximo de 10 mA do datasheet. É eletricamente irrelevante a 115200 bps: com ~10 pF de
capacitância de pino, a constante RC fica em ~10 ns contra um bit de 8,7 µs.

> ✅ **A direção oposta está confirmada segura, com números.** `VOH` do GPS é
> ≥ `VCC − 0,4 V` a IOH = 4 mA (Tabela 10) — ou seja 2,9 V com VCC de 3,3 V, ou 2,6 V com
> VCC de 3,0 V. O `VIH` do RP2350 é ~0,65 × 3,3 = 2,15 V. Ambos os casos leem nível alto
> com margem. **Nenhum divisor ou level shifter é necessário no `GPS TX → GPIO 1`.**

#### 🔋 Bateria de backup e EEPROM

A família GY-* normalmente inclui bateria de backup recarregável e **EEPROM (24C32)**
para persistir configuração. ⚠️ *Característica da família de placas, não verificada na
sua unidade.*

Se a EEPROM estiver presente, o comando `CFG-CFG` do RF01.2 persiste a configuração em
memória não volátil, e ela sobrevive mesmo com a bateria descarregada. **O firmware deve
reenviar a configuração a cada boot de qualquer forma** — é defensivo, custa alguns
milissegundos e cobre o caso de placa sem EEPROM ou de EEPROM não gravada.

#### Consequência para o orçamento de corrente (R-13)

Consumo do GPS conforme datasheet (UBX-15031086, Tabela 11):

| Parâmetro | Valor | Nota |
| :--- | ---: | :--- |
| `ICCP` — pico máximo | **67 mA** | *"use esta figura para dimensionar a fonte"* |
| `ICC` tracking contínuo (GPS+GLONASS) | 30 mA @ 3,0 V | regime normal de condução |
| `ICC` aquisição (até o 1º fix) | 32 mA @ 3,0 V | |
| `ICC_RF` — antena ativa (LNA) | até **50 mA** | corrente **separada**, do pino VCC_RF |

Número de projeto do bloco GPS: **~82 mA** (67 mA de pico + ~15 mA de LNA de antena
patch típica). Com antena que consuma o máximo de `ICC_RF`, chega a 117 mA.

Alimentando em 5 V, essa carga inteira sai do regulador interno do Pico e passa para o
LDO da placa GPS. O `3V3_OUT` fica então com display (~50–80 mA com backlight) e picos de
escrita do SD (até ~100 mA) — margem bem mais confortável.

⚠️ *Medir de todo modo:* consumo agregado no `3V3_OUT` com todos os periféricos ativos e
backlight em 100%.

### ⚠️ 4. Periféricos de Interface (Encoder KY-040 + LED RGB)

#### ⚠️ Pinagem física do KY-040 (conferida na placa, 2026-09-17)

Da esquerda para a direita, olhando de frente:

| # | Serigrafia | Liga em |
| :---: | :--- | :--- |
| 1 | `GND` | GND comum |
| 2 | `+` | `3V3_OUT` (pino 36) |
| 3 | `SW` | GPIO 4 (pino 6) |
| 4 | `DT` | GPIO 3 (pino 5) |
| 5 | `CLK` | GPIO 2 (pino 4) |

> ⚠️ **Esta ordem é o INVERSO da que costuma aparecer documentada** para o KY-040.
> Confira na sua placa antes de soldar. A revisão 2 deste documento tinha a sequência
> ao contrário, o que colocaria **3,3 V e GND diretamente em dois GPIO**.
* ⚠️ **Debounce do encoder (novo na rev. 2):** solde um **capacitor cerâmico de 100 nF**
  entre **CLK e GND** e outro entre **DT e GND**, o mais próximo possível do módulo. O
  KY-040 é eletricamente ruidoso; sem este filtro o ajuste de brilho salta de forma
  errática mesmo com decodificação por máquina de estados em software (RF04).

* ⚠️ **LED RGB Pino R (Vermelho):** **Resistor de 330 Ω** → **Pico GPIO 6 (Pino 9)**.
* ⚠️ **LED RGB Pino G (Verde):** **Resistor de 68 Ω** → **Pico GPIO 7 (Pino 10)**.
* ⚠️ **LED RGB Pino B (Azul):** **Resistor de 68 Ω** → **Pico GPIO 8 (Pino 11)**.
* **LED RGB Cátodo Comum:** direto ao GND comum.

> ⚠️ Os valores de verde e azul mudaram de 330 Ω para 68 Ω. Ver a **nota crítica** do
> BOM: com 330 Ω esses canais não acendem, e a Zona de Semáforo (RF03.3) depende do
> canal verde para produzir amarelo.


> ⚠️ **O LED verde de status de Wi-Fi saiu do projeto** (decisão do autor, 2026-09-15).
> Os estados de boot, sem sinal, OTA e falha de dados são exibidos **somente na tela**
> (`requirements.md` RF03.9), tornando o indicador dedicado redundante. Com isso:
>
> * O **GPIO 14 (pino 19)** fica **livre** para uso futuro.
> * O LED RGB é o **único** indicador luminoso do projeto, e sinaliza exclusivamente
>   estado de via.

### ⚠️ 5. Circuito de Potência do Buzzer Remoto Oculto

> **Correção da revisão 1 — ✅ confirmada pelo autor em 2026-09-15:** o Jack P2 (3,5 mm)
> foi **substituído por conector JST-XH de 2 vias**. No arranjo anterior (tip = 5 V, sleeve = coletor), inserir ou remover o
> plugue faz o sleeve varrer o tip; com o transistor conduzindo nesse instante, o
> resultado é curto direto de 5 V ao GND **sem o buzzer limitando a corrente** — risco
> de dano ao transistor e ao carregador veicular. Conector de áudio para energia é
> antipadrão conhecido justamente por isso.

* **Pico GPIO 5 (Pino 7)** → **Resistor de 1 kΩ**.
* Outro lado do **Resistor de 1 kΩ** → perna **BASE** do transistor (pino central do
  TO-92).
* Pino **EMISSOR** do transistor → **GND comum**.
* Pino **COLETOR** do transistor → terminal **negativo** do **conector JST-XH fêmea**
  montado no gabinete.
* Linha de **5 V** (nó `VSYS` filtrado, após o Schottky) → terminal **positivo** do
  conector JST-XH fêmea.
* **Diodo 1N4148 (opcional):** se mantido, solde em paralelo nos terminais do conector,
  com a listra (catodo) no terminal de 5 V e o lado sem listra (anodo) no terminal do
  coletor. Ver nota do BOM: não é necessário para buzzer piezoelétrico.
* **Extensão do Painel:** fio **positivo (+)** do Buzzer SFM-27 no pino positivo do
  **JST-XH macho**, fio **negativo (−)** no pino negativo.

> ⚠️ *Confirmar a pinagem do transistor no datasheet.* A ordem E-B-C do BC337 e do
> 2N2222 **difere** da do BC547 descrita na revisão 1. Não assuma a mesma disposição.
>
> *Nota de montagem:* o JST-XH não tem trava mecânica forte como um Molex. Prenda o
> cabo com abraçadeira perto do gabinete para que vibração e tração no cabo do painel
> não atuem sobre os pinos soldados.

---

## ✅ Verificação Pré-Energização

Antes de ligar o circuito pela primeira vez:

- [ ] Continuidade entre `GND (38)` e todos os terras dos módulos.
- [ ] **Ausência** de continuidade entre `VSYS (39)` e `GND (38)` (curto de alimentação).
- [ ] Polaridade do capacitor eletrolítico (faixa cinza no GND).
- [ ] Orientação do diodo Schottky (catodo/faixa apontando para o Pico).
- [ ] Pinagem do transistor conferida no datasheet do modelo adquirido.
- [ ] **Regulador de 3 pinos confirmado na placa GPS** antes de aplicar 5 V (seção 3).
- [ ] Resistor de 1 kΩ em série no caminho `GPIO 0 → GPS RX`.
- [ ] Nenhum módulo de 3,3 V **sem regulador** conectado à linha de 5 V.
- [ ] Cartão SD formatado em FAT32, com `radares.bin` e `wifi.cfg` presentes.
- [ ] Medir tensão em `3V3_OUT` com todos os periféricos conectados e backlight em 100%.

---

## 🔗 Rastreabilidade da Revisão 2

| Item de `revisao_tecnica.md` | Onde foi aplicado |
| :--- | :--- |
| R-01 — `VBUS` é o pino 40; usar `VSYS` + Schottky | Seção 1; BOM item 14 |
| R-05 — 330 Ω apaga verde e azul do LED RGB | BOM itens 11 e 12; seção 4; nota crítica |
| R-06 — Jack P2 curto-circuita 5 V no GND | Seção 5; BOM item 19 — ✅ **confirmado** |
| R-13 — Orçamento de corrente do `3V3_OUT` | Nota na seção 3; checklist |
| R-14 — Alimentação do GPS indefinida | Seção 3 — ✅ **5 V decidido** (GY-GPSV3 tem LDO embarcado) |
| R-22 — GPIO de 3,3 V excede `VIN` do GPS | Seção 3 — resistor de 1 kΩ em série no `GPIO 0 → GPS RX` |
| R-15 — Mutex no SPI0 compartilhado | Nota de firmware na seção 2 |
| R-16 — Justificativa incorreta do diodo | Nota do BOM item 15 |
| R-17 — BC547 sem margem de corrente | BOM item 10; nota; seção 5 |
| R-19 — Zona de Semáforo depende do canal verde | Nota crítica do BOM |
| L-05 — Faixa térmica | BOM item 16 (105 °C) |
| E-01 — Item numerado 29 em vez de 19 | BOM renumerado 1–20 |
| RF04 — Debounce do encoder | BOM item 18; seção 4 |
| Pinagens físicas conferidas | Seções 2 e 4 — leitor SD tem 8 pinos; KY-040 estava invertido |
| Card detect | Seção 2 — `DET` → GPIO 14; distingue cartão ausente de ilegível (RF07) |

**Confirmado pelo autor (2026-09-15):** conector **JST-XH** no lugar do Jack P2 (seção 5).

**Removido do projeto (2026-09-15):** LED verde de status de Wi-Fi e seu resistor de
330 Ω. O `GPIO 14` liberado passou a ser usado pelo **card detect** do leitor SD
(2026-09-17).

Pendente na seção 3: **confirmar visualmente que a placa GY-GPSV3 tem regulador de
3 pinos junto ao VCC** antes de aplicar os 5 V. Verificação de 30 segundos; errar custa
o módulo.

Medições pendentes: Vf dos LEDs (R-05), consumo do buzzer (R-17), corrente do
`3V3_OUT` (R-13).
