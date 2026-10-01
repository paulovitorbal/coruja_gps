# 🔧 Montagem Física na Caixa

**Projeto:** Detector de Radares GPS Inteligente (Raspberry Pi Pico 2 W)
**Caixa:** Patola PB-111/TE — 123 × 85 × 85 mm externos, ABS injetado
**Complementa:** `bom_schematic.md` (circuito) e `gera_fritzing.py` (fiação)
**Data:** 2026-10-01

---

## 0. Estado desta montagem — leia antes do resto

> ### ✋ Monta-se com o que já existe. O resto é contingente.
>
> **Decisão do autor em 2026-10-01, registrada no [ADR 0011](docs/adr/0011-medir-antes-de-mitigar.md).**
>
> A montagem a executar **agora** usa a **Patola PB-111** comprada e o módulo
> **GY-GPSV3-NEO M8N** com o **patch cerâmico de 8 cm direto no U.FL**.
>
> As mitigações térmicas discutidas em 2026-10-01 — antena de cabo longo com
> bulkhead SMA, caixa impressa em ASA ou PC, tampa de parede dupla em colmeia,
> ventilação, manta isolante, dissipador interno, troca para NEO-M8M — **não
> são backlog**. O gatilho é **observar problema**, e os instrumentos são o
> **M-08** e o **M-06** (§9). Nada se compra, imprime ou refaz antes de haver
> número.

| Tier | O quê | Quando |
| :--- | :--- | :--- |
| ✅ **Agora** | Chassi + VHB (§2, §3) · prateleira no chassi em camadas (§4) · Kapton (§5) · patch de 8 cm direto no U.FL · **buzzer levado para perto do ouvido** (2 m de cabo já na BOM, ataca o R-32) | executar |
| 🔶 **Barato, oportunista** | Pés com folga de ar · manta isolante refletiva sob a caixa · rasgos de ventilação | se der na mão, sem prioridade |
| ⏸️ **Contingente** | **Antena de cabo longo + bulkhead SMA** (§4.8 — primeira ordem) · caixa impressa · colmeia · dissipador interno · NEO-M8M | só se o M-08 ou o M-06 acusarem |

## 0.1 Por que este documento existe

O projeto documentava o circuito e a fiação, e **nada** sobre como as peças se
prendem umas às outras e à caixa. Isso virou problema concreto na hora de montar:
duas placas perfuradas de geometria herdada, um fundo de caixa sem ancoragem e um
módulo GPS cuja orientação não é negociável.

As decisões abaixo foram tomadas sobre a montagem real, não sobre um projeto ideal.
Onde a escolha foi condicionada pelo que já existia, está dito.

---

## 1. A caixa e a orientação

A orientação é a primeira decisão porque **tudo depende dela** — em especial a antena
do GPS, que é direcional.

| Face | Medida | Destino |
| :--- | :---: | :--- |
| Frontal | 123 × 85 mm | voltada ao **motorista** — display e encoder |
| Tampa (peça clara) | 123 × 85 mm | voltada ao **céu** — passagem da antena do GPS |
| Laterais | 85 × 85 mm | conectores e passa-cabo |

Consequências que vieram de graça com essa orientação:

- O patch cerâmico aponta para o **zênite**, que é o melhor caso possível.
- O display fica na face frontal, **fora** do caminho entre a antena e o céu. Se ele
  estivesse na tampa, a moldura metálica e a camada refletora do TFT atenuariam o
  sinal de forma significativa.
- Entre o cerâmico e o céu há apenas ar e ABS, que é transparente em RF.

> ⚠️ **As torres do fundo são para a TAMPA**, não para placa de circuito. A descrição
> da revenda afirma o contrário e está errada — conferido na peça. **O fundo não tem
> nenhuma ancoragem para PCB**, e é desse fato que decorre a solução da seção 2.

---

## 2. A base: uma placa perfurada servindo de chassi

### O problema

Duas placas perfuradas, ambas aproveitadas do que havia em casa:

| Placa | Conteúdo | Formato |
| :--- | :--- | :--- |
| A | step-down de 12 V | estreita e longa |
| B | Pico 2 W | acomoda o Pico com 4 furos sobrando de cada lado |

Nenhuma das duas foi projetada para ser fixada, e os furos não correspondem a nada.

### A solução

**Uma terceira placa perfurada de fibra como chassi**, à qual as duas se prendem; a
caixa recebe só o chassi. Isso troca um encaixe impossível por dois encaixes fáceis.

A razão de ser placa perfurada, e não acrílico: a placa traz **malha de 2,54 mm em
toda a superfície**. Não se mede nem se fura nada para posicionar as placas — escolhe-se
o furo. Com geometria herdada, isso elimina a etapa que gerou o problema. Acrílico
exigiria gabarito, e acrílico fino racha ao ser furado sem cuidado.

Três ganhos, e o terceiro é o que importa num carro:

1. A furação do chassi é livre; a da caixa, não.
2. O conjunto sai inteiro num bloco — essencial num projeto que ainda muda.
3. **As duas placas passam a vibrar juntas.** Fixadas separadamente, elas se movem uma
   em relação à outra e isso fadiga as juntas de solda dos fios que as ligam. É um
   defeito que apareceria depois de meses e seria caçado como mau contato.

### Especificação do chassi

| Item | Escolha | Por quê |
| :--- | :--- | :--- |
| Material | **FR4** (fibra, esverdeada) | Fenolite é quebradiça, absorve umidade e racha com vibração. Aqui a placa é **estrutura**. |
| Face do cobre | voltada para **baixo** | A face de cima fica só substrato; o cobre encosta em ABS, que é isolante. |
| Recorte | em volta das torres da tampa | Travamento lateral de graça (ver seção 3). |

### Nota sobre as duas placas separadas

Herdado, mas **favorável**: o step-down é um conversor chaveado e o GPS tem front-end
sensível. Tê-los em placas distintas permite afastá-los fisicamente e orientar o laço
de alta `di/dt` do conversor longe da antena. Numa placa única — o plano para a v2 —
essa separação precisa ser planejada de propósito, porque deixa de ser automática.

---

## 3. Fixação da base na caixa

Como o fundo não tem torre de PCB, em ordem de preferência:

### 3.1 Fita VHB na face inteira — escolhido

O chassi apresenta quase 100 cm² de área plana contra ABS plano. VHB em ABS com essa
área segura muito mais do que o conjunto pesa, e tem faixa de temperatura compatível
com uso automotivo. Não fura a caixa.

**Custo:** remover depois dá trabalho — álcool isopropílico, linha de costura e
paciência.

### 3.2 Parafuso soberba de fora para dentro — alternativa serviçável

Fura o fundo e parafusa por baixo, em espaçadores no chassi. Fica desmontável de
verdade. A cabeça do parafuso fica na face que apoia no suporte, então na prática não
incomoda.

### 3.3 Travamento lateral pelas torres — fazer nos dois casos

Recortar o chassi para encaixar em volta das torres da tampa. As torres passam a
impedir deslocamento lateral sem custo nenhum, e o adesivo ou o parafuso só precisam
impedir que o chassi levante — o esforço mais fácil dos dois.

Vibração em carro é predominantemente lateral, e **travamento mecânico é melhor que
adesivo para isso**.

> 📏 Conferir qual parafuso acompanha a caixa antes de furar: torre de Patola é para
> rosca soberba, e o diâmetro do furo depende dele.

---

## 4. A prateleira do módulo GPS

> ⚠️ **Revisado em 2026-10-01 (R-65).** A versão anterior punha a prateleira **na
> tampa**. A tampa é a face solar, e isso colocava o componente termicamente limitante
> do projeto no ponto mais quente da caixa. A prateleira passa a nascer do **chassi**.

**Placa perfurada de fibra sobre colunas que nascem do chassi**, com a antena no topo
olhando o zênite e o módulo **sob** ela. O ponto de metal no centro do cerâmico é o
pino de alimentação do patch, e confirma qual face vai para cima.

### 4.1 Vista lateral — corte pelo lado de 85 × 85

```
                                ↑  céu · satélites
                                │
          ╔══════════════════════════════════════════════╗
          ║ ===  rasgo alto                              ║   TAMPA · cinza-claro
          ║                                              ║   face solar · só plástico
          ║ ·  ·  ·  ·  folga de ar  ·  ·  ·  ·  ·  ·  · ║   QUEBRA TÉRMICA
  ┌─────┐ ║        ┌──────────────────────────┐          ║
  │     │ ║        │   ANTENA   ·   cerâmica  │          ║   vê o zênite
  │  D  │ ║ ┏━━━━━━┷━━━━━━━━━━━━━━━━━━━━━━━━━━┷━━━━━━━━┓ ║
  │  I  │ ║ ┃ Kapton                                   ┃ ║   PRATELEIRA · FR4
  │  S  │ ║ ┃        ┌───────────────────────┐         ┃ ║   escudo de radiação
  │  P  │ ║ ┃        │   NEO-M8N  ·  sombra  │         ┃ ║
  │  L  │ ║ ┗━━━━━━━━┷━━━━━━━━━━━━━━━━━━━━━━━┷━━━━━━━━━┛ ║
  │  A  │ ║      ┃                           ┃           ║   colunas · do chassi
  │  Y  │ ║   ┌──┃────────┐     ┌────────────┃─────────┐ ║
  │     │ ║   │ step-down │     │   Pico 2 W           │ ║
  └─────┘ ║   └───────────┘     └──────────────────────┘ ║
          ║ ════════════════════════════════════════════ ║   CHASSI · perfurada
          ║ ===  rasgo baixo                             ║
          ╚══════════════════════════════════════════════╝   fundo ABS · VHB
               oo                                 oo         pés · folga de ar
          ════════════════════════════════════════════════════
                         PAINEL   ·   92 °C
```

### 4.2 As camadas, e o que cada uma resolve

| Camada | Função |
| :--- | :--- |
| **Tampa** | Face solar. Só plástico, cor clara, **nada metálico** acima do cerâmico. |
| **Folga de ar** | Quebra térmica entre a tampa aquecida e a antena. Custa zero em RF. |
| **Antena cerâmica** | No topo da prateleira. Precisa de vista de céu; é peça feita para viver no painel. |
| **Prateleira (FR4)** | **Escudo de radiação** — bloqueia o infravermelho que a tampa reemite para baixo. Kapton na face de cima (§5). |
| **Módulo NEO-M8N** | **Sob** a prateleira, na sombra dela. É o componente térmico limitante (RNF09, R-65). |
| **Colunas** | Nascem do **chassi**, não da tampa. |
| **Chassi** | Pico e step-down (§2). |
| **Pés** | Folga de ar contra o painel — corta a condução dos 92 °C, que é o acoplamento mais forte. |

### 4.3 Os quatro ganhos de nascer do chassi

1. **A abertura da tampa não mexe no conjunto GPS.** É o ganho mais importante e veio
   de uma objeção do autor. A entrada de RF alimenta o LNA por *bias tee* e é sensível
   a ESD, e o conector é a peça mais frágil da montagem. Com a prateleira na tampa, ele
   flexionava a cada manutenção. Agora fica imóvel depois de montado uma vez.
2. **O módulo sai da face solar** — era o pior lugar para o componente limitante.
3. **A prateleira vira escudo** entre a tampa quente e o módulo.
4. **Antena e módulo seguem lado a lado**, com coaxial curto e sem esforço no conector.

> 💡 Uma versão intermediária desta proposta separava antena (na tampa) e módulo (no
> chassi). Resolvia a térmica e **punha a peça mais frágil a flexionar** a cada abertura.
> Registrado para não ser reinventado.

### 4.4 Cuidados mecânicos

- **Quatro pontos de apoio, não dois.** Massa em balanço sobre placa fina num ambiente
  que vibra.
- **Colunas curtas.** Quanto menor o braço, menor o risco de ressonância.
- Nada metálico entre o patch e o céu. A tampa tem de seguir sendo só plástico na
  área acima do cerâmico.
- **Dois rasgos de ventilação**, um baixo e um alto, criando convecção. Caixa selada ao
  sol é estufa, e o interior de carro não é ambiente sujo o bastante para justificar
  vedação.

### 4.5 Blindagem contra EMI — adiada até haver medição

A intenção inicial era fita aluminizada aterrada na face inferior da prateleira.
**Decisão: montar sem, medir, e só então decidir.**

O raciocínio é que a folha atua em um dos dois caminhos de acoplamento, e não no que
domina nesta montagem:

| Caminho | A folha ajuda? |
| :--- | :--- |
| Radiado de campo próximo — do chaveamento para a antena e o front-end | sim |
| **Conduzido** — pela alimentação e pelos fios de sinal até o Pico | **não** |

Em montagem com fios voando, o conduzido tende a dominar: os fios do GPS atravessam a
região ruidosa e são tanto condutor quanto antena. Blindar o plano e deixar os fios
passando ao lado do indutor do conversor é tapar a janela e deixar a porta aberta.

Somando, o ganho é incerto — um plano condutor próximo pode **desafinar** o patch, que
é projetado para um plano de terra específico — enquanto os modos de falha são
concretos (seção 6.3).

> 🔎 **E há um terceiro motivo, que só apareceu ao reler a lista de materiais.** O item
> 2 é um NEO-M8N **com antena ativa externa SMA** — a antena é peça separada, num cabo.
> Logo o caminho de RF até o módulo é **coaxial, que é blindado**, e não um patch nu
> numa placa a centímetros do conversor chaveado. Isso já resolve boa parte do que
> motivou a ideia da folha, e resolve melhor do que a folha resolveria.



**Se a medição indicar problema**, atacar nesta ordem, que é a de efeito decrescente:

1. **Filtrar a alimentação do módulo:** ferrite bead + 10 µF no pino VCC. Ataca o
   caminho dominante e custa centavos.
2. **Distância e orientação** entre o conversor e a antena.
3. **Roteamento dos fios** (seção 7).
4. **O laço de alta `di/dt` do conversor.** É o laço *capacitor de entrada → chave de
   cima → chave de baixo → volta ao capacitor*, onde a corrente é picada; não é o
   indutor, cuja função é justamente alisá-la. Uma espira com corrente variando rápido
   é um dipolo magnético, e o campo irradiado é proporcional à **área** do laço — o
   mesmo princípio da nota do cabo trançado de 12 V em `bom_schematic.md`, vista do
   lado da emissão em vez da captação.

   **Como o conversor é módulo pronto, esse laço está no PCB dele e fora de alcance.**
   O que está em alcance é impedir que ele **vaze para a fiação**: com capacitância de
   entrada insuficiente no módulo, parte da corrente picada vem dos fios de 12 V e o
   laço cresce para incluí-los. Um eletrolítico de bulk mais um cerâmico **nos
   terminais de entrada do módulo** mantêm a corrente picada local a ele.
5. Só então a folha. E nesse caso, **fita de cobre com adesivo condutivo**, nunca
   alumínio (seção 6.3).

### 4.6 Como medir, em vez de supor

Não testar a mitigação — **testar a fonte de ruído**. Mesmo local, mesma vista de céu,
mesmo módulo, comparando o C/N0 médio da mensagem **GSV** (o parser do projeto já a
decodifica):

- alimentado pelo **USB**, sem o step-down de 12 V;
- alimentado pelo **step-down**.

Sem diferença, o chaveamento não está acoplando e a folha não tem o que resolver.

| C/N0 (dB-Hz) | Leitura |
| :---: | :--- |
| 40–50 | satélite forte, céu aberto — sem problema a resolver |
| 30–40 | faixa típica |
| < 30 | rastreio marginal — investigar |

> 🪟 **Fator provavelmente maior que toda a EMI interna:** para-brisa atérmico
> metalizado atenua GNSS de forma significativa, e é por isso que esses carros têm uma
> área sem metalização reservada a sensor e pedágio. Conferir se o veículo tem essa
> janela demarcada — se tiver, a posição da caixa no painel passa a ser a variável
> dominante.

### 4.7 Uma propriedade da arquitetura a não perder

O CYW43 transmite em 2,4 GHz a centímetros de um front-end que trabalha em 1575 MHz
com sinal na casa de −130 dBm. Transmissor forte e próximo dessensibiliza receptor
sensível, e isso seria sério — **só que os dois nunca precisam operar juntos**: o OTA
só roda com o veículo parado (RF05.1) e o Wi-Fi desconecta ao terminar.

A arquitetura resolveu isso antes de existir hardware. **Mudanças futuras não devem
quebrar essa propriedade sem perceber.**

---

### 4.8 O caminho da v2: bulkhead SMA na parede ⏸️ contingente

> ⏸️ **Não executar agora** — ADR 0011. Registrado porque é a mitigação de
> **primeira ordem** do R-65 e a ordem de grandeza não é óbvia.

O conector do módulo é **U.FL / IPEX MHF1**, confirmado a paquímetro em
~2 mm de diâmetro na fêmea da placa. O datasheet da Hirose especifica **30
ciclos de encaixe** para a vida inteira da peça: é conector de montagem, não
de manutenção.

> ⚠️ A BOM dizia "antena ativa externa SMA". Estava errado, e o erro fez duas
> propostas desta sessão nascerem mortas — caixa na coluna A e antena remota,
> ambas supondo cabo roteável onde há 8 cm.

```
   módulo ──U.FL── rabicho curto ──SMA fêmea de painel │ parede de 85 × 85
                                                        │
                                   antena externa ──────┘
```

Quatro coisas de uma vez:

1. O **U.FL é encaixado uma única vez**, na montagem, e fica aliviado de
   tração — nunca mais tocado.
2. A **fronteira de manutenção passa a ser SMA**, rosqueado e robusto.
3. A **antena troca por fora**, sem abrir a caixa.
4. **Abrir a tampa deixa de tocar em RF** — resolve a objeção original de
   forma mais completa que a prateleira no chassi, que elimina o
   flexionamento mas deixa o conector exposto ao serviço.

**E destrava o que importa:** com SMA na parede, entra qualquer puck
automotivo de 3 a 5 m, e **a caixa deixa de precisar de vista de céu**. Ela
vai para onde é fresco e cômodo — baixa, na sombra da aba do painel. Com 8 cm,
a exigência de céu prega a caixa no ponto mais ensolarado do carro, e é por
isso que esta é a única mitigação de primeira ordem.

A perda do cabo longo é quase de graça porque a antena é **ativa**: pela
fórmula de Friis, perda que vem **depois** de um LNA de 20 a 28 dB entra
dividida por algumas centenas. É a razão de existirem antenas ativas — com
antena passiva, 5 m seriam proibitivos.

#### Manuseio do U.FL, nas duas ou três vezes

- **Perpendicular, pressão no corpo do conector.** Nunca empurrar nem puxar
  pelo cabo — é assim que essas peças morrem.
- **Soltar reto para cima**, com extrator ou alavancando sob o invólucro.
- **Alívio de tração a um ou dois centímetros**, para o esforço morrer na
  âncora. ⚠️ Se usar adesivo, **cura neutra** (§6.1): acetoxi a um centímetro
  de uma entrada de RF é a pior combinação do documento.
- **Raio de curva mínimo de ~5 mm.** Coaxial de 1,13 mm dobrado rente ao corpo
  fratura a malha.
- Com pulseira ESD: o pino central vai à entrada do LNA pelo *bias tee*.
- Tranquilidade: **30 ciclos é o orçamento, não 2.** Bancada, desmontagem e
  montagem definitiva não chegam perto.

#### O que comprar

**Rabicho U.FL macho → SMA fêmea de bulkhead**, 10 a 20 cm, com porca e
arruela para furo de ~6,3 mm numa face de 85 × 85, que hoje não tem nada.

> ⚠️ **SMA, não RP-SMA.** A maioria dos rabichos é RP-SMA porque o mercado é
> Wi-Fi. São parecidos e incompatíveis, e antena GPS automotiva é SMA **macho**
> — a parede precisa de SMA **fêmea** de verdade, com soquete e não com pino.

As duas arquiteturas são **mutuamente exclusivas**: o patch de 8 cm termina em
U.FL e não entra num bulkhead SMA. Adotando a v2, ele vira antena de bancada e
sobressalente.

## 5. Isolamento elétrico

| Onde | Como | Por quê |
| :--- | :--- | :--- |
| Entre o módulo GPS e a prateleira | **Kapton** na face superior | Isola o módulo das ilhas de cobre da placa. Nada a ver com EMI — vale independentemente da seção 4.5. |
| Sob cada placa, no chassi | **espaçador de ≥ 3 mm** | O lado de baixo de placa perfurada é um tapete de perna cortada. Perna encostando em qualquer coisa é curto esperando a hora. |
| Face do cobre do chassi | voltada para **baixo**, contra o ABS | Evita um plano de pontos condutores soltos virado para dentro da caixa. |
| Entre ilhas de cobre e qualquer folha metálica | Kapton, ou placa sem cobre | Caso a blindagem da seção 4.5 venha a ser adotada. |

> ❌ **Nunca colar placa direto com VHB.** A fita tem 1 a 2 mm e as pernas de solda são
> mais altas que isso: a placa ficaria apoiada nas pontas de solda, que é o pior apoio
> possível. VHB serve para o **chassi** contra a caixa, onde as duas faces são planas.

---

## 6. Adesivos: o que usar e o que não usar

### 6.1 ❌ Silicone RTV automotivo

**Não usar em contato com a eletrônica.** A maioria dos RTV baratos, inclusive os
automotivos de vedação, é de **cura acetoxi**: libera **ácido acético** ao curar. O
vapor condensa nos terminais e nas juntas de solda e corrói cobre, estanho e latão —
chega a descolar a solda do fio. É falha retardada: funciona bem, e meses depois
aparece verdete nas ilhas e um intermitente que se caça no software.

Agrava-se aqui porque silicone cura **por umidade do ar, de fora para dentro**: um
cordão grosso sob um módulo fica muito tempo sem curar no meio, mantendo o ácido preso
encostado na placa.

**Teste:** apertar um pouco num papel e cheirar. **Cheiro forte de vinagre = acetoxi.**
Cura neutra (alcoxi ou oxima) solta álcool e tem cheiro fraco. Na embalagem, procurar
"cura neutra", "não corrosivo" ou "para eletrônica" — *não* "automotivo" nem "alta
temperatura", que nada dizem sobre a cura.

"Alta temperatura" é, aliás, a especificação errada: Pico, display, GPS e cartão somam
cerca de 2 W. Pagar por 300 °C não compra nada e costuma vir justo com a química
corrosiva, porque é silicone de junta de motor.

Uso aceitável: **na caixa, não na eletrônica** — vedar a tampa contra pó, ou fazer o
passa-cabo. E curar com a caixa vazia antes de pôr as placas.

### 6.2 ❌ Cola quente

O reflexo natural, e errado aqui. EVA amolece na faixa de 60 a 80 °C, e painel ao sol
passa bem disso. A montagem desaba num estacionamento à tarde.

Quem esquenta não é o circuito — é o **carro**. Essa é a temperatura que seleciona o
adesivo.

### 6.3 ⚠️ Fita aluminizada

Se a blindagem da seção 4.5 vier a ser adotada, **não usar alumínio**:

- A maioria das fitas de alumínio tem **adesivo acrílico isolante**. Tiras sobrepostas
  **não** se conectam, e o resultado são ilhas isoladas em vez de um plano. É o erro
  prático mais comum do assunto.
- **Alumínio não solda** com estanho e fluxo comuns — o óxido não deixa. "Aterrada"
  exigiria conexão mecânica, parafuso com arruela mordendo a folha.

**Usar fita de cobre com adesivo condutivo:** solda normalmente, as sobreposições
conduzem, e o fio de terra se prende com um ponto de solda.

E dois cuidados que valem para qualquer folha:

- **Aterrar curto e em vários pontos**, no terra do **próprio módulo GPS**. Blindagem
  aterrada por um ponto só e com fio longo pode ser **pior que blindagem nenhuma**: a
  indutância do fio faz com que aquilo não seja terra na frequência do ruído, e a folha
  passa a ser antena ressonante que acopla ruído para dentro. Se o fio não puder ser
  curto, é melhor deixar a folha flutuando.
- **Isolar a face externa da folha.** Um plano de terra nu virado para as placas e a
  fiação é curto esperando a hora. A ordem é folha, depois isolante, e o isolante é que
  olha para o circuito.

### 6.4 ✅ Resumo

| Uso | Escolha |
| :--- | :--- |
| Chassi na caixa | **VHB** (ou soberba por fora) |
| Placas no chassi | **espaçador de nylon** + parafuso M2 ou M2,5 |
| Isolamento sob o módulo GPS | **Kapton** |
| Vedação da tampa / passa-cabo | silicone de **cura neutra**, curado com a caixa vazia |
| Blindagem, se necessária | **fita de cobre** com adesivo condutivo |

> 🔩 Para parafusar nos furos de placa perfurada: a malha é de 2,54 mm e os furos têm
> cerca de 1 mm, então qualquer parafuso exige alargamento. **M2 ou M2,5, não M3** —
> M3 pede 3,2 mm e come duas ilhas vizinhas. Na borda não faz diferença, mas M2 é mais
> limpo.

---

## 7. Gerenciamento de cabos

Há bastante fio indo para peças de painel — display, encoder — e para o cartão. Em
instalação automotiva, **a fadiga por vibração acontece na saída do fio da junta de
solda**, e é a falha mais provável da montagem a médio prazo.

| Prática | Motivo |
| :--- | :--- |
| **Âncoras de abraçadeira** coladas no chassi, com os fios passando por elas | Transfere o esforço da junta de solda para a âncora. Custa centavos e é o item de melhor relação da lista. |
| **VCC trançado com GND** nos fios do GPS | Reduz a área do laço, que é o que acopla ruído. |
| **Par do GPS curto**, longe do indutor do step-down e das linhas SPI do display | Os fios do GPS atravessam a região ruidosa e são tanto condutor quanto antena (seção 4.5). |
| Folga suficiente para **abrir a tampa** sem tracionar nada | A prateleira do GPS está na tampa: ela se move junto, e o fio dela é o que mais sofre. |
| Fio do display sem dobra fechada no **flex** | O flex do TFT flexiona; cola ou dobra rígida criam ponto de fadiga. |

---

## 8. O que não pode ser coberto, em nenhuma hipótese

| Peça | Consequência de cobrir |
| :--- | :--- |
| **Furo de som do buzzer** | Abafa o volume. Levaria a procurar erro no PWM onde não há. |
| **Flex e vidro do display** | Ponto de fadiga no flex. Silicone na borda do bezel é aceitável; no flex, não. |
| **Área da tampa acima do patch cerâmico** | Bloqueia o céu e inutiliza o GPS. |
| Slot do microSD, eixo do encoder | Por motivo óbvio. |

---

## 9. Pendências

Medições e verificações que faltam. Numeração própria (**M-NN**) para não colidir com
os **R-NN** de `revisao_tecnica.md`.

| Item | O que falta | Trava o quê |
| :--- | :--- | :--- |
| **M-01** | Área livre no fundo, **entre as torres** | Define o tamanho do chassi — primeira medida a tirar. |
| **M-02** | Altura das torres a partir do fundo | Define quanto sobra para o andar de cima. |
| **M-03** | Dimensões externas do módulo do display, **com o flex** | Confere se display e encoder cabem confortáveis na face de 123 × 85. Pelo olho cabem, mas a conta não foi feita com a peça na mão. |
| **M-04** | Parafuso que acompanha a caixa | Define o furo do chassi (seção 3). |
| **M-05** | Ordem dos eixos confirmada na peça | Como duas medidas são 85 mm, a dúvida é só qual face leva os 123 mm. A orientação da seção 1 assume o que foi descrito. |
| **M-06** | A/B de C/N0 pela GSV, USB × step-down | Decide a blindagem da seção 4.5. |
| **M-07** | Para-brisa do veículo é atérmico? | Se for, domina tudo o que está na seção 4. |
| **M-08** | **Campanha térmica:** um dia inteiro parado ao sol, na posição de montagem real, registrando temperatura a cada minuto | Fecha o **R-65**. O RP2350 tem sensor de temperatura **no próprio die**, no canal 4 do ADC, e o firmware não usa o ADC para nada — há um termômetro ocioso dentro do aparelho e um `LoggerCartao` que já grava no cartão. É o único número que importa, e substitui toda a especulação. ⚠️ O sensor lê a pastilha, que corre acima do ambiente por autoaquecimento: caracterizar o desvio uma vez em ambiente conhecido. |
| **R-13** | Corrente agregada e temperatura do LDO do display | Já aberto em `revisao_tecnica.md`; a caixa fechada muda o regime térmico e torna a medição mais relevante, não menos. |

> ⚠️ **M-01 a M-05** são régua e paquímetro, e **M-01 a M-04 precisam estar fechados
> antes de cortar o chassi**. **M-06, M-07 e M-08** exigem o veículo — e o M-08 exige
> um dia de sol, então vale deixar o registro pronto antes do próximo verão.
