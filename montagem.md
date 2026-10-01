# 🔧 Montagem Física na Caixa

**Projeto:** Detector de Radares GPS Inteligente (Raspberry Pi Pico 2 W)
**Caixa:** Patola PB-111/TE — 123 × 85 × 85 mm externos, ABS injetado
**Complementa:** `bom_schematic.md` (circuito) e `gera_fritzing.py` (fiação)
**Data:** 2026-10-01

---

## 0. Por que este documento existe

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

**Placa perfurada de fibra presa na tampa com espaçadores**, com o módulo em cima e o
patch cerâmico voltado para o céu. O ponto de metal no centro do cerâmico é o pino de
alimentação do patch, e confirma qual face vai para cima.

Por que na tampa e não no chassi: é o único lugar que garante o patch no zênite com a
caixa na orientação da seção 1.

### Cuidados mecânicos

- **Quatro pontos de apoio, não dois.** A tampa é ABS fino, e a prateleira é massa em
  balanço sobre placa flexível num ambiente que vibra.
- **Espaçadores curtos.** Quanto menor o braço, menor o risco de ressonância.
- Nada metálico entre o patch e o céu. A tampa tem de seguir sendo só plástico na
  área acima do cerâmico.

### 4.1 Blindagem contra EMI — adiada até haver medição

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

**Se a medição indicar problema**, atacar nesta ordem, que é a de efeito decrescente:

1. **Filtrar a alimentação do módulo:** ferrite bead + 10 µF no pino VCC. Ataca o
   caminho dominante e custa centavos.
2. **Distância e orientação** entre o conversor e a antena.
3. **Roteamento dos fios** (seção 7).
4. **O laço do conversor.** Em placa perfurada o laço entrada-chave-diodo costuma ser
   enorme, e ele é o radiador — a fonte, não o sintoma.
5. Só então a folha. E nesse caso, **fita de cobre com adesivo condutivo**, nunca
   alumínio (seção 6.3).

### 4.2 Como medir, em vez de supor

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

### 4.3 Uma propriedade da arquitetura a não perder

O CYW43 transmite em 2,4 GHz a centímetros de um front-end que trabalha em 1575 MHz
com sinal na casa de −130 dBm. Transmissor forte e próximo dessensibiliza receptor
sensível, e isso seria sério — **só que os dois nunca precisam operar juntos**: o OTA
só roda com o veículo parado (RF05.1) e o Wi-Fi desconecta ao terminar.

A arquitetura resolveu isso antes de existir hardware. **Mudanças futuras não devem
quebrar essa propriedade sem perceber.**

---

## 5. Isolamento elétrico

| Onde | Como | Por quê |
| :--- | :--- | :--- |
| Entre o módulo GPS e a prateleira | **Kapton** na face superior | Isola o módulo das ilhas de cobre da placa. Nada a ver com EMI — vale independentemente da seção 4.1. |
| Sob cada placa, no chassi | **espaçador de ≥ 3 mm** | O lado de baixo de placa perfurada é um tapete de perna cortada. Perna encostando em qualquer coisa é curto esperando a hora. |
| Face do cobre do chassi | voltada para **baixo**, contra o ABS | Evita um plano de pontos condutores soltos virado para dentro da caixa. |
| Entre ilhas de cobre e qualquer folha metálica | Kapton, ou placa sem cobre | Caso a blindagem da seção 4.1 venha a ser adotada. |

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

Se a blindagem da seção 4.1 vier a ser adotada, **não usar alumínio**:

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
| **Par do GPS curto**, longe do indutor do step-down e das linhas SPI do display | Os fios do GPS atravessam a região ruidosa e são tanto condutor quanto antena (seção 4.1). |
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
| **M-06** | A/B de C/N0 pela GSV, USB × step-down | Decide a blindagem da seção 4.1. |
| **M-07** | Para-brisa do veículo é atérmico? | Se for, domina tudo o que está na seção 4. |
| **R-13** | Corrente agregada e temperatura do LDO do display | Já aberto em `revisao_tecnica.md`; a caixa fechada muda o regime térmico e torna a medição mais relevante, não menos. |

> ⚠️ Fora **M-06** e **M-07**, nada aqui exige o veículo. M-01 a M-05 são régua e
> paquímetro, e **M-01 a M-04 precisam estar fechados antes de cortar o chassi**.
