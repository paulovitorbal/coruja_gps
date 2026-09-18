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
| 5 | ⚠️ **Display IPS TFT 2,4"** | Colorido, **320×240 pixels**, interface SPI com pino de Backlight (BL). **Controlador a confirmar:** 2,4" costuma ser ILI9341, não ST7789 — a sequência de inicialização difere | Exibir velocidade, limites e alertas visuais. |
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
| 18 | ⚠️ **Conector JST-XH 2 vias** | Par macho + fêmea, com cabo | **Saída do buzzer.** Substitui o Jack P2: polarizado e sem contato deslizante. |
| 19 | ⚠️ **Conector JST-XH 3 vias** | Par macho + fêmea. **Pino central sem uso** | **Entrada de 5 V**, depois do conversor. Três vias de propósito, para não encaixar no conector de 2 vias do buzzer. |
| 20 | **Placa Perfurada (Perfboard)** | Fenolite ou fibra com furos metalizados (pitch 2,54 mm) | Base de montagem do circuito. |
| 21 | **Barras de Pinos Fêmea 1x20** | Duas fileiras, espaçamento 2,54 mm | Soquete para encaixar e remover o Pico 2 W sem soldá-lo direto. |
| 22 | 🆕 **Conversor CC 12 V → 5 V** | Buck. **Entrada ≥ 40 V** (*load dump*). Saída ≥ 1 A, preferir **ajustável** | Alimenta o aparelho. Montado **fora** do gabinete, por ocupar espaço. Proteger com termorretrátil. |
| 23 | 🆕 **Adaptador de fusível piggyback** | *"add-a-circuit"*, do tipo de fusível da caixa do seu carro (mini, padrão ou micro2) | Deriva um circuito pós-chave na caixa de fusíveis **sem emenda no chicote**. Reversível. |
| 24 | 🆕 **Fusível de 2 A** | Do mesmo tipo do adaptador | Protege a derivação. **Não use 10 A** — ver nota de dimensionamento. |
| 25 | **Cabo 1,5 mm² (2 vias)** | Da caixa de fusíveis ao **conversor** (trecho de 12 V) | Sobredimensionado para a carga (~180 mA), o que é seguro. |
| 26 | 🆕 **Fio 22 AWG, cobre estanhado** | 0,35 mm². Cores variadas — ver convenção abaixo | Fiação **interna** do gabinete, cabo do **conversor ao gabinete** (5 V) e cabo até o buzzer. |
| 27 | 🆕 ⚠️ **Diodo TVS bidirecional 24 V** | **P6KE24CA** (600 W) ou **1.5KE24CA** (1500 W). Axial, **bidirecional** — sufixo `CA` | **Na entrada de 12 V do conversor.** Clampa transientes da rede do carro. Ver nota de dimensionamento. |
| 28 | 🆕 **Capacitor Eletrolítico 470 µF / 50 V** | **50 V é obrigatório aqui**, não 25 V. 105 °C — atenção à polaridade | **Na entrada de 12 V**, em paralelo com o TVS. Segura o que o TVS não pega e amortece a queda na partida. |

### ⚠️ Nota — fiação interna em 22 AWG (item 26)

O 22 AWG (0,35 mm²) suporta ~3 A contínuo, contra um máximo de **310 mA** em qualquer
ramo interno — margem de 10× no pior caso. A queda de tensão é desprezível em todos:

| Ramo | Corrente | Compr. | Queda (ida e volta) |
| :--- | ---: | ---: | ---: |
| 12 V da entrada ao conversor | 200 mA | 15 cm | 3,2 mV |
| 5 V do conversor ao `VSYS` | 310 mA | 10 cm | 3,3 mV |
| GND geral | 310 mA | 15 cm | 4,9 mV |
| 3V3 ao display e cartão | 220 mA | 12 cm | 2,8 mV |
| **Conversor ao gabinete (5 V)** | **310 mA** | **1,5 m** | **49 mV** |
| Buzzer, cabo até o painel | 50 mA | 2 m | 10,6 mV |

**Cobre estanhado é a escolha certa aqui**, e não só pela solda: o estanho protege
contra oxidação, o que importa num ambiente com variação térmica e umidade como o
interior de um painel.

> 💡 **Convenção de cor sugerida**, espelhando as cores dos fios no Fritzing: preto para
> GND, vermelho para 5 V e 12 V, azul para 3V3. Nos sinais, qualquer cor — mas manter a
> mesma do esquemático poupa conferência durante a montagem.

### ⚠️ Nota — os dois conectores do aparelho

O gabinete tem **dois conectores externos**, ambos em ~5 V:

| Conector | Vias | Pino A | Pino B |
| :--- | :---: | :--- | :--- |
| **Entrada** (item 19) | **3** (central sem uso) | +5 V do conversor | GND |
| **Buzzer** (item 18) | **2** | +5 V do trilho `VSYS` | Coletor do transistor |

Contagens diferentes impedem a troca. Com o conversor **fora** do gabinete, a troca
deixou de ser destrutiva — é o que mudou em relação à revisão anterior:

* Plugue do buzzer na entrada → buzzer recebe 5 V e GND e **toca continuamente**.
  Irritante, não destrutivo.
* Plugue da entrada no buzzer → o GND da alimentação encosta no coletor; o aparelho
  **não liga**. Sem dano.

Então as 3 vias passaram de **medida de segurança** a **medida de robustez**: evita
duas formas de perder tempo depurando, ao mesmo custo. Vale manter.

> 🔴 **O perigo real migrou para o trecho de 12 V.** Ver *"O trecho de 12 V NÃO pode
> usar JST-XH"* na seção 0 — é lá que um plugue errado destrói o aparelho.

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

### 🆕 ⚠️ Nota — proteção da entrada de 12 V (itens 27 e 28)

**O que estava desprotegido.** O fusível de 2 A protege o **fio** contra curto, não o
conversor contra sobretensão: fusível não reage a um transiente de dezenas de volts por
milissegundos. E o `C1`/`C2` da seção 1 ficam em `VSYS`, ou seja **depois** do
conversor — protegem o Pico, não a fonte dele. O conversor era o único componente
exposto à rede do carro, e o desenho contava com ele aguentar sozinho. Registrado como
**R-31**.

**Por que 24 V de standoff.** O clamp precisa ficar numa janela estreita:

| Limite | Valor | Por quê |
| :--- | ---: | :--- |
| Piso | **> 16 V** | a rede é 13,8 a 14,4 V com o motor ligado; clampar abaixo disso conduziria em operação normal e queimaria o diodo |
| Teto | **< 40 V** | máximo absoluto do LM2596 é 45 V; o clamp tem de proteger antes disso |

O **24CA** clampa em **33,2 V** — no meio da janela. Para comparação, um `1.5KE30CA`
clampa em 41,4 V, colado no limite do chip, e um `47CA` clampa em 65 V, que não protege
nada. **O sufixo `CA` é obrigatório**: significa bidirecional. A versão `A` é
unidirecional e, montada ao contrário, fica em curto permanente.

**Honestidade sobre o que isso resolve.** Um *load dump* real — bateria se desconectando
com o alternador carregando — chega a 60 a 120 V com impedância de fonte de ordem de
ohms, por até 400 ms. Contra um clamp de 33 V isso dá dezenas de ampères e **mais de
1 kW sustentado**; nem 600 W nem 1500 W de rating de pulso (10/1000 µs) sobrevivem a
isso. Alternador de carro moderno já clampa o load dump internamente em ~35 V, e é nisso
que o projeto se apoia.

O que o TVS **de fato** resolve são os transientes frequentes e de baixa energia:
chaveamento de cargas indutivas, ruído de ignição, os pulsos da ISO 7637-2. São esses
que matam módulo barato no uso diário, e para esses 600 W bastam. É proteção bem
investida, não blindagem contra tudo.

**Por que o eletrolítico da entrada é 50 V e o da saída é 25 V.** São papéis diferentes.
Na entrada de 12 V a tensão nominal é margem real contra transiente, e 50 V é o mínimo
sensato. Em `VSYS`, que fica em ~4,7 V, 25 V já é folga de 5× — ali o que importa é
capacitância e o rating de **105 °C**, porque o painel passa de 60 °C e a vida de um
eletrolítico cai pela metade a cada 10 °C.

---

## 🔌 Esquema de Conexões e Roteamento (Fritzing)

Mapeamento de nós para interligar os componentes na aba "Esquemático" ou "Protoboard".

### 🆕 0. Instalação no veículo

A alimentação vem do **pós-chave**, derivada na caixa de fusíveis. O conversor CC fica
**fora do gabinete** — ocupa espaço demais dentro — e o conector de entrada do aparelho
vem **depois** dele. O que entra no gabinete é **5 V**.

```
bateria ─ caixa de fusíveis ─┬─ [circuito original do carro]
                             └─ piggyback ─ fusível 2 A
                                    │
                                    │  1,5 mm², 12 V
                                    ▼
                          ┌─── proteção da entrada ───┐
                          │  TVS P6KE24CA   ┬  470 µF │  itens 27 e 28
                          │  (bidirecional) │   50 V  │  em paralelo, junto
                          └────────┬────────┴─────────┘  ao conversor
                                    │
                                    ▼
                          ┌──────────────────────┐
                          │ conversor 12 V → 5 V │   fora do gabinete,
                          │  (ajustável)         │   sob o painel
                          └──────────┬───────────┘
                                     │  22 AWG, 5 V
                                     ▼
                          JST-XH 3 vias  (entrada do gabinete)
                                     │
                          Schottky ─ VSYS (pino 39)      ← seção 1
```

#### 🔴 O trecho de 12 V NÃO pode usar JST-XH

Com o conversor fora, existem agora **dois cabos externos**: um de 12 V (piggyback →
conversor) e um de 5 V (conversor → gabinete). Se os dois usarem JST-XH, o de 12 V
encaixa no conector do gabinete e injeta **12 V no nó `VSYS`**, cujo máximo absoluto é
5,5 V — Pico, GPS, display e cartão destruídos juntos.

Duas formas de impedir, escolha uma:

* **Ligue o 12 V direto aos terminais do conversor**, sem conector. É o mais simples:
  não existindo plugue de 12 V, não há o que trocar.
* Se quiser conector no 12 V, use **família diferente** — JST-VH (passo 3,96 mm) ou
  faston. Nunca XH.

> O risco mudou de lugar, não desapareceu. Antes o 12 V entrava no gabinete e o perigo
> estava nos dois conectores do aparelho; agora o 12 V para no conversor e o perigo
> está entre os dois cabos externos.

#### Por que pós-chave e não bateria direta com relé

O pós-chave permanece energizado durante a partida do motor — **confirmado pelo autor
no veículo**. A bateria cai a ~9–10 V enquanto o arranque gira, mas não desaparece, e um
buck de entrada larga regula 5 V normalmente nessa faixa. Logo **não há reinício na
ignição**, que era a única razão real para bateria auxiliar ou supercapacitor.

Um relé acionado pelo pós-chave liberando a bateria direta foi considerado e
**descartado**: a bobina de um relé automotivo consome 80 a 150 mA, contra **~180 mA do
aparelho inteiro** — gastaria quase tanto quanto liga. E o benefício dele (consumo
parasita zero) já vem da própria chave de ignição, que corta o pós-chave.

#### ⚠️ Dimensionamento do fusível: 2 A, não 10 A

**Fusível protege o fio, não a carga.** Consumo no lado de 12 V:

| Tensão de entrada | Corrente |
| :--- | ---: |
| 13,8 V (motor ligado) | ~132 mA |
| 12,6 V (bateria em repouso) | ~145 mA |
| 9,0 V (durante a partida) | ~203 mA |

São ~1,6 W. Um fusível de **10 A só atua em curto franco**: uma falta intermediária —
isolamento raspado gerando 6 A — não o abriria, mas aqueceria o condutor. **2 A** dá
margem de 10× sobre a carga e protege de verdade.

O cabo de 1,5 mm² suporta ~15 A e está sobredimensionado de propósito, o que é seguro —
mas o fusível acompanha a **carga**, e o cabo só precisa não ser o elo fraco.

#### 💡 Ajuste a saída do conversor para compensar o Schottky

O Schottky da seção 1 derruba 0,3–0,45 V. Com o conversor em 5,00 V, o `VSYS` fica em
~4,6 V — o Pico lida bem com isso, mas o buzzer, que é alimentado do `VSYS`, sai um
pouco mais fraco.

Se o seu módulo for ajustável, regule a saída para **5,35–5,45 V**: o `VSYS` chega a
~5,0 V e o buzzer trabalha na tensão nominal.

> ⚠️ **Limite:** o `VSYS` do Pico aceita no máximo **5,5 V**. Não passe de **5,8 V** na
> saída do conversor, e **meça** antes de conectar à placa. Ajuste com a placa
> desligada.

#### ⚠️ Load dump e polaridade reversa

O transiente que destrói eletrônica automotiva não é a partida, é o **load dump** —
quando a carga do alternador sai abruptamente, o 12 V pode chegar a **60–120 V por
dezenas de milissegundos**. Módulos buck comuns são especificados para 35–40 V e podem
não sobreviver.

* Especifique o conversor para **≥ 40 V de entrada**, ou acrescente um **TVS** no 12 V.
* **Polaridade reversa:** o Schottky protege o lado de 5 V. No 12 V não há proteção —
  uma inversão chega direta ao conversor. Um diodo em série ou TVS resolve.

Com o conversor fora do gabinete, ele fica exposto a vibração e umidade: proteja com
termorretrátil ou uma caixinha própria. Dissipação não é preocupação — a ~85% de
eficiência são ~0,25 W.

---

#### 🆕 Montagem da proteção de entrada (itens 27 e 28)

Os dois componentes ficam **em paralelo entre `+12 V` e `GND`**, fisicamente o mais perto
possível dos terminais de entrada do conversor — laço de corrente curto é metade da
eficácia de um supressor.

* **TVS P6KE24CA:** um terminal em `+12 V`, o outro em `GND`. **Bidirecional, portanto
  sem polaridade** — é o único componente desta seção que pode ser montado em qualquer
  sentido, e é justamente por isso que o sufixo `CA` importa.
* **Eletrolítico 470 µF / 50 V:** terminal positivo `(+)` em `+12 V`, negativo `(−)`,
  o do lado da faixa, em `GND`. **Polaridade invertida aqui explode o capacitor**, com
  12 V e um fusível de 2 A para alimentar o erro.

Ordem ao longo do cabo, da caixa de fusíveis para o aparelho:

```
piggyback ─ fusível 2 A ─── 1,5 mm² ───┬──── TVS ────┬──── conversor ─── 5 V
                                       │             │
                                       └─ 470 µF/50 V┘
                                       └──── GND ────┘
```

O fusível vem **antes** da proteção, não depois: se o TVS falhar em curto — que é o modo
de falha desejável dele — o fusível é o que interrompe a corrente. TVS em curto sem
fusível a montante vira aquecedor ligado na bateria.

> ⚠️ **Proteja o conjunto com termorretrátil** e fixe contra vibração, como o conversor.
> São três componentes soldados num cabo, sob o painel, num carro em movimento.

### ⚠️ 1. Barramento de Entrada e Filtro Duplo de Energia

> **Correção da revisão 1:** o documento anterior mandava a entrada para "VBUS (Pino
> 39)". No pinout real do Pico 2, **pino 39 = `VSYS`** e **pino 40 = `VBUS`** — o rótulo
> estava trocado. Além disso, `VBUS` é ligado diretamente ao conector USB: injetar 5 V
> externo ali cria conflito de fontes quando o USB é usado para gravação ou debug. A
> entrada correta é `VSYS`, através de diodo Schottky.
>
> **Revisão 4:** o 5 V deixou de vir de carregador USB veicular e passa a vir do
> conversor CC da seção 0, **externo** ao gabinete, através do JST de 3 vias.

* **Pino 1 do JST de entrada** (+5 V, vindo do conversor externo) → **Anodo** do
  **Diodo Schottky 1N5819**.
* **Catodo do Schottky** (lado da faixa) → pino **`VSYS` (Pino 39)** do Pico 2 W.
* **Pino 3 do JST de entrada** (GND) → pino **`GND` (Pino 38)** do Pico 2 W.
  *(Pino 2 do JST fica sem uso.)*

> *Nota:* o Pico tem **oito pinos de GND** — 3, 8, 13, 18, 23, 28, 33 e 38 — todos
> internamente ligados. O pino 38 é a referência deste documento por ficar ao lado do
> `VSYS` (39), o que encurta o laço de corrente do par de capacitores. Use os demais
> livremente para distribuir terra na perfboard; o esquemático do Fritzing usa também o
> **pino 3**, próximo aos periféricos de sinal.
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
* **Alimentação do Bloco:** conecte o pino **`3V`** do leitor SD no pino
  **`3V3_OUT` (Pino 36)**. O **VCC da tela** depende da inspeção da seção 3: **5 V** se o
  módulo tiver regulador próprio (preferível — tira o backlight do trilho de 3V3),
  `3V3_OUT` se não tiver. Conecte os **GND** de ambos ao GND comum.
* **PWM do backlight:** use frequência **≥ 20 kHz**. Abaixo de ~1 kHz o painel
  cintila de forma perceptível na visão periférica e pode bater com feições da estrada
  em efeito estroboscópico; entre 1 e 20 kHz alguns módulos assobiam. Ver §8 do
  `formato_dados.md` para a curva de brilho.

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
LDO da placa GPS. O `3V3_OUT` fica então com display e picos de escrita do SD (até
~100 mA).

⚠️ **O display de 2,4" mudou essa conta (rev. 5).** O backlight de um painel de 2,4"
ilumina ~4× a área do de 1,3" e usa tipicamente 4 LEDs em vez de 1 ou 2, então a
estimativa anterior de 50–80 mA não se aplica mais. Isso torna a medição do **R-13** mais
crítica, não menos.

> 🔍 **Inspeção antes de ligar — mesma lógica do R-14 (GPS).** Muitos módulos de 2,4"
> trazem **regulador próprio e aceitam 5 V na entrada**. Se o seu tiver, ligue o `VCC`
> do display nos **5 V**, não no `3V3_OUT`: o backlight inteiro sai do conversor CC e o
> trilho de 3V3 do Pico volta a ter folga, eliminando o risco do R-13. Procure o
> encapsulamento de 3 pinos junto ao `VCC` e leia a serigrafia da entrada (`5V` vs
> apenas `3.3V`). **Sem regulador, 5 V destroem o módulo.**
>
> 🔍 Verifique também se o módulo traz **slot de microSD embutido**. Se trouxer, pode
> tornar o leitor Adafruit (item 6) redundante — mas seria o mesmo SPI0, e o mutex
> display ↔ SD continua obrigatório.

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
> de dano ao transistor e à fonte de 5 V. Conector de áudio para energia é
> antipadrão conhecido justamente por isso.

* **Pico GPIO 5 (Pino 7)** → **Resistor de 1 kΩ**.
* Outro lado do **Resistor de 1 kΩ** → perna **BASE** do transistor (pino central do
  TO-92).
* Pino **EMISSOR** do transistor → **GND comum**.
* Pino **COLETOR** do transistor → terminal **negativo** do **conector JST-XH fêmea**
  montado no gabinete.
* Linha de **5 V** (nó `VSYS` filtrado, após o Schottky) → terminal **positivo** do
  conector JST-XH fêmea **de 2 vias** (item 18).

> 🔴 **O conector do buzzer tem 2 vias e o da entrada de 12 V tem 3, de propósito.**
> Se ambos fossem de 2 vias, plugar a entrada de 12 V no soquete do buzzer injetaria
> 12 V no nó `VSYS` — máximo 5,5 V — destruindo Pico, GPS, display e cartão de uma vez.
> Ver a nota crítica dos conectores no início deste documento.
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

- [ ] **Fusível de 2 A** instalado no adaptador piggyback, em slot **pós-chave**.
- [ ] Conversor CC **fora** do gabinete, protegido contra vibração e umidade.
- [ ] Saída do conversor **medida** antes de ligar à placa: 5,0 V, ou 5,35–5,45 V se
      for compensar o Schottky. **Nunca acima de 5,8 V.**
- [ ] **Trecho de 12 V sem conector JST-XH** — direto no conversor, ou família diferente.
- [ ] 🆕 **TVS de 24 V instalado** na entrada de 12 V, junto ao conversor. Confirmar que
      é o sufixo **`CA`** (bidirecional), não `A`.
- [ ] 🆕 **Polaridade do eletrolítico de 50 V da entrada** — faixa no GND. Invertido
      com 12 V atrás, ele explode.
- [ ] 🆕 Fusível **antes** da proteção no percurso do cabo, nunca depois.
- [ ] **Conectores de entrada (3 vias) e de buzzer (2 vias) confirmados diferentes.**
- [ ] Polaridade da entrada de 5 V conferida no JST, com o cabo já crimpado.
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
| Instalação no veículo | Seção 0 — pós-chave via piggyback, fusível de 2 A, **conversor externo** |
| Conectores permutáveis | Seção 0 (12 V, crítico) e nota dos conectores do aparelho (5 V, robustez) |
| Fiação interna | BOM item 26 — 22 AWG estanhado, margem de 10× e quedas < 11 mV |

**Confirmado pelo autor (2026-09-15):** conector **JST-XH** no lugar do Jack P2 (seção 5).

**Removido do projeto (2026-09-15):** LED verde de status de Wi-Fi e seu resistor de
330 Ω. O `GPIO 14` liberado passou a ser usado pelo **card detect** do leitor SD
(2026-09-17).

Pendente na seção 3: **confirmar visualmente que a placa GY-GPSV3 tem regulador de
3 pinos junto ao VCC** antes de aplicar os 5 V. Verificação de 30 segundos; errar custa
o módulo.

Medições pendentes: Vf dos LEDs (R-05), consumo do buzzer (R-17), corrente do
`3V3_OUT` (R-13).
