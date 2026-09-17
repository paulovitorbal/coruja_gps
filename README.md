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

`coruja_gps.fzz` é **gerado**, não editado à mão:

```bash
python3 gera_fritzing.py
```

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
