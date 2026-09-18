# coruja_gps — detector de radares embarcado

Projeto pessoal de estudo em sistemas embarcados: um alertador de radares e semáforos
sobre **Raspberry Pi Pico 2 W**, com firmware em **C++17 / Pico SDK**.

Lê telemetria de um GPS NEO-M8N por UART, confronta a posição com uma base local de
18.294 pontos e sinaliza em quatro zonas por display, LED RGB periférico e buzzer.

## Base de pontos

O firmware carrega `radares.bin`, um binário compacto de 12 bytes por ponto,
ordenado por latitude. Ele é produzido por `converte.py` a partir de um CSV no
**padrão iGO8**:

```
X,Y,TYPE,SPEED,DirType,Direction
-44.021044,-19.799916,1,30,1,257
```

```bash
python3 converte.py base_igo8.txt radares.bin
```

**Outro formato de entrada?** Escreva seu próprio parser — e não reimplemente o
formato binário. `formato_radares.py` expõe as enumerações e o escritor:

```python
from formato_radares import Ponto, TipoPonto, Sentido, escreve

pontos = [Ponto(lat=-19.799916, lon=-44.021044, limite=30, rumo=257,
                tipo=TipoPonto.RADAR_FIXO, sentido=Sentido.UNIDIRECIONAL)]
escreve(pontos, "radares.bin")
```

O `escreve()` cuida de quantização, flags, **ordenação por latitude** e CRC-32 — as
quatro coisas que um parser erraria em silêncio. A ordenação é a pior: a busca
binária do firmware depende dela e erra *sem avisar* se faltar.

Para testar, `formato_radares.le()` relê com as mesmas validações do firmware: se
aceita, o firmware carrega. Use `converte.py` (padrão iGO8) como modelo.

Obter a base é problema de quem monta: o projeto não presume fonte nenhuma.

---

## Alimentação e instalação no veículo

Alimentado pelo **pós-chave**, derivado na caixa de fusíveis. O conversor CC fica **fora
do gabinete** — ocupa espaço demais dentro — então o que entra no aparelho é **5 V**.

```
bateria ─ caixa de fusíveis ─ piggyback ─ fusível 2 A ─ 1,5 mm² (12 V)
                                                             │
                                          ┌──────────────────▼─────────┐
                                          │ TVS 24 V  ┬  470 µF / 50 V │ proteção da
                                          │  (bidir.) │               │ entrada
                                          └──────────────────┬─────────┘
                                          ┌──────────────────▼─────────┐
                                          │ conversor 12 V → 5 V       │ fora,
                                          │ ajustável, entrada ≥ 40 V  │ sob o painel
                                          └──────────────────┬─────────┘
                                                             │ 22 AWG (5 V)
                                          ┌──────────────────▼─────────┐
                                          │ JST 3 vias → Schottky      │ gabinete
                                          │   → VSYS (pino 39)         │
                                          └────────────────────────────┘
```

Consumo: **~145 mA em 12 V** (≈1,6 W) em condução normal, com pico de ~200 mA quando a
tensão cai a 9 V durante a partida. No lado de 5 V são ~162 mA normais e ~310 mA no
pior caso sustentado.

### Proteção da entrada de 12 V

O fusível protege o **fio** contra curto; ele não reage a sobretensão. E os capacitores
de filtro ficam em `VSYS`, **depois** do conversor — protegem o Pico, não a fonte dele.
Sem mais nada, o conversor é o único componente exposto direto à rede do carro.

Daí o **TVS bidirecional de 24 V** (`P6KE24CA` ou `1.5KE24CA`) e um **eletrolítico de
470 µF / 50 V** em paralelo na entrada, junto ao conversor. O clamp de 24 V cai numa
janela estreita: tem de ficar acima de 16 V, porque a rede é 13,8 a 14,4 V com o motor
ligado, e abaixo de 40 V, porque é o máximo do conversor. O `24CA` clampa em 33,2 V.

**O que isso não resolve:** um *load dump* real chega a 60–120 V por até 400 ms, o que
contra um clamp de 33 V significa mais de 1 kW sustentado — nenhum TVS axial pequeno
sobrevive. O que ele cobre são os transientes frequentes e de baixa energia, que são os
que matam módulo barato no uso diário. Para o load dump, o projeto se apoia em o
alternador de carro moderno já clampar internamente em ~35 V, e isso é **suposição, não
medição**.

Dois detalhes que estragam tudo se errados: o sufixo **`CA`** significa bidirecional, e
a versão `A` montada ao contrário fica em curto permanente; e o **fusível vai antes** da
proteção no percurso do cabo, porque o modo de falha desejável de um TVS é curto.

### 🔴 O trecho de 12 V não pode usar o mesmo conector do aparelho

Com o conversor fora, há **dois cabos externos**: 12 V (piggyback → conversor) e 5 V
(conversor → gabinete). Se ambos usarem JST-XH, o de 12 V encaixa na entrada do
aparelho e injeta **12 V no nó `VSYS`** — máximo absoluto 5,5 V. Pico, GPS, display e
cartão destruídos juntos.

**Ligue o 12 V direto aos terminais do conversor, sem conector.** Não existindo plugue
de 12 V, não há o que trocar. Se quiser conector ali, use família diferente (VH,
faston) — nunca XH.

### Três decisões que não são óbvias

**Pós-chave em vez de bateria direta com relé.** O pós-chave permanece energizado
durante a partida, então não há reinício na ignição. O relé foi descartado porque a
bobina consome 80–150 mA contra ~180 mA do aparelho inteiro — gastaria quase tanto
quanto liga — e o benefício dele (consumo parasita zero) já vem da chave de ignição.

**Fusível de 2 A, não 10 A.** Fusível protege o fio, não a carga. Com 10 A, uma falta
intermediária de 6 A não abriria o fusível mas aqueceria o condutor. O cabo de 1,5 mm²
suporta ~15 A e está sobredimensionado de propósito — mas o fusível acompanha a carga.

**Conversor com entrada ≥ 40 V.** O transiente que destrói eletrônica automotiva não é
a partida, é o *load dump*: 60–120 V por dezenas de milissegundos quando a carga do
alternador sai. Módulos comuns de 35 V podem não sobreviver.

### Ajuste opcional da saída do conversor

O Schottky derruba 0,3–0,45 V, então 5,00 V na saída deixam o `VSYS` em ~4,6 V. Se o
módulo for ajustável, regule para **5,35–5,45 V** e o `VSYS` chega a ~5,0 V, com o
buzzer na tensão nominal. **Nunca acima de 5,8 V** — o `VSYS` aceita no máximo 5,5 V.
Meça antes de conectar.

### Conectores do aparelho

| Conector | Vias | Pino A | Pino B |
| :--- | :---: | :--- | :--- |
| Entrada | **3** (central sem uso) | +5 V | GND |
| Buzzer | **2** | +5 V (`VSYS`) | Coletor |

Contagens diferentes impedem a troca. Com o conversor fora, trocá-los deixou de ser
destrutivo — o pior caso é o buzzer tocar sozinho ou o aparelho não ligar — então as
3 vias são medida de **robustez**, não de segurança. O perigo real está no trecho de
12 V, acima.

## Documentos

Leia nesta ordem:

| Documento | O que é |
| :--- | :--- |
| **`requirements.md`** | Requisitos funcionais e não-funcionais, matriz de interação homem-máquina e fluxo de telas. É a especificação. |
| **`bom_schematic.md`** | Lista de materiais e roteamento pino a pino, com as pinagens físicas conferidas nas placas. |
| **`formato_dados.md`** | Formato binário `radares.bin`, estratégia de carga em RAM e busca geográfica em dois estágios. |
| **`revisao_tecnica.md`** | Revisão técnica com rastreabilidade: o que foi achado, decidido e o que segue aberto. |
| **`roteiro_bancada.html`** | Procedimento de medição para imprimir, com quadro de registro das leituras. |

## Estado

**Decidido:** C++17 com Pico SDK · GPS+GLONASS a 4 Hz (piso 3 Hz) · conector JST-XH ·
GPS alimentado em 5 V · Zona de Semáforo silenciosa · buzzer escalonado em 3 faixas ·
LED RGB exclusivo de estado de via.

**Pendente de bancada** — ver `roteiro_bancada.html`:

| Item | Medida |
| :--- | :--- |
| R-05 | Vf dos LEDs verde e azul, e calibração das razões R:G (amarelo) e R:B (rosa) |
| R-13 | Tensão do trilho 3V3 sob carga, com backlight em 100% |
| R-17 | Consumo do buzzer e pinagem do transistor |
| R-14 | Inspeção: regulador de 3 pinos na placa GPS — **sem ele, 5 V destroem o módulo** |
| SD `DET` | Polaridade do card detect, com e sem cartão |

**Escopo futuro:** desenho de telas e interfaces visuais.

## Esquemático no Fritzing

`coruja_gps.fzz` foi **gerado** e depois **ajustado à mão** no Fritzing:

```bash
python3 gera_fritzing.py    # ⚠️ sobrescreve o .fzz e DESCARTA o ajuste manual
```

> ⚠️ **O `.fzz` versionado contém trabalho manual que o gerador não reproduz:**
> roteamento dos fios com pontos de dobra, orientação de 16 peças e 4 notas rotulando
> os módulos representados por headers genéricos. Regerar descarta tudo isso.
>
> O gerador é a fonte da **netlist**; o `.fzz` é a fonte do **layout**. Ao mudar a
> fiação, o caminho é editar `NETS`, regerar num arquivo temporário, comparar as redes
> e então aplicar a mudança à mão no Fritzing — não sobrescrever.
>
> **Divergência conhecida:** o gerador já traz a entrada do gabinete com 3 vias
> (`J5V`, +5 V · n/c · GND), mas o `.fzz` versionado ainda mostra a entrada antiga de
> 2 vias, rotulada "carregador veicular". O documento de referência para montagem é o
> `bom_schematic.md`.

A netlist vive em `NETS`, dentro de `gera_fritzing.py`, transcrita do
`bom_schematic.md`. Mudou a fiação? Edite ali e regenere.

O `fritzing_geo.py` calcula a posição real de cada pino: lê o SVG de breadboard da
peça, resolve o mapeamento `connector → svgId` do `.fzp` e aplica os transforms de
grupo. Sem isso os fios sairiam desenhados fora dos pinos. A escala da cena
(90 unidades por polegada) foi medida empiricamente contra um sketch existente.

**Dependências do gerador:** Fritzing instalado (a biblioteca de peças é lida de
`/Applications/Fritzing.app`) e um sketch com a peça do Pico, de onde ela é
reaproveitada e embutida no `.fzz`.

Quatro módulos não têm peça na biblioteca — NEO-M8N, ST7789, leitor microSD e
KY-040 — e são representados por headers fêmea rotulados, com a contagem correta de
pinos. A peça de microSD que existe na biblioteca tem **zero conectores**, é
inutilizável.

Na vista protoboard há fios reais, coloridos por rede: **preto** = GND,
**vermelho** = 5 V, **azul** = 3V3, e cores distintas por grupo de sinal. Esquemático
e PCB usam conexões diretas, exibidas como ratsnest.

## Datasheets

| Arquivo | O que é |
| :--- | :--- |
| `NEO-M8_datasheet_UBX-15031086-R14.pdf` | Datasheet u-blox do módulo GPS, **revisão R14**. É a revisão citada em todas as análises deste projeto. |
| `NEO-M8_datasheet_UBX-15031086-R11.pdf` | Mesmo documento, **revisão R11** — anterior. Mantida para comparação; em caso de divergência, vale a R14. |
| `adafruit-microsd-spi-sdio.pdf` | Guia do breakout microSD. Fonte da polaridade do `DET` (cartão presente = ALTO, pull-up de 4,7 kΩ na placa) e da confirmação de que há pull-up em todos os pinos SPI. |

Valores extraídos do datasheet do GPS e usados no projeto: limites elétricos
(`VCC` 2,7–3,6 V, `VIN` 0–`VCC`, `IPIN` 10 mA), correntes (`ICCP` 67 mA, tracking
30 mA, `ICC_RF` até 50 mA), faixa térmica (−40 a +85 °C, **operação e
armazenamento**) e teto de taxa de navegação (**5 Hz** em GPS+GLONASS, 10 Hz em
GNSS único).
