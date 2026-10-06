# 🔧 Montagem Física — **v2**

**Projeto:** Detector de Radares GPS Inteligente (Raspberry Pi Pico 2 W)
**Caixa:** impressa em 3D, ASA — 130 × 130 × 67 mm externos
**Placa:** PCB fabricada, 120 × 120 mm, 2 camadas
**Complementa:** `bom_schematic.md` (circuito) e `pcb.md` (placa)
**Data:** 2026-10-05

---

## 0. O que mudou da v1, e por quê

A v1, de 2026-10-01, descrevia uma montagem **artesanal dentro de uma caixa
comprada**: Patola PB-111, chassi de placa perfurada colado com VHB, e o módulo
GPS numa prateleira de fibra presa à tampa com espaçadores.

Em 2026-10-05 três decisões do autor derrubaram essa arquitetura inteira.

| # | Mudança | O que motivou |
| :-- | :--- | :--- |
| 1 | **Placa fabricada** no lugar de placa perfurada | o projeto foi para o KiCad e a PCB foi roteada e verificada |
| 2 | **Caixa impressa** no lugar da Patola | a caixa passa a seguir a placa, e não o contrário |
| 3 | **Antena na PCB** no lugar da prateleira | *"a montagem em prateleira ficou muito bagunçada e ruim de fechar"* |

### A terceira é a mais consequente, e resolve o problema mais duro do projeto

O **R-67** era a restrição mais dura que havia: o rabicho da antena tem **8 cm**,
e o U.FL aguenta **30 ciclos de encaixe**. Na v1, módulo e antena ficavam em
planos diferentes — módulo no chassi, antena na prateleira da tampa —, e os 8 cm
precisavam vencer essa distância toda, atravessando a abertura da caixa, a cada
abertura e fechamento.

Com os dois **vizinhos na mesma placa**, sobra cabo. O U.FL deixa de ser tocado
na manutenção rotineira.

Ganham-se ainda duas coisas que a prateleira de fibra não dava:

- **plano de terra sob a antena** — que é o que um patch cerâmico quer;
- **distância do conversor chaveado**, que fica na extremidade oposta da placa.
  O ruído vem do chaveamento e do indutor do LM2596, não dos capacitores.

> 📌 **O que a v1 ainda vale.** As medições e a química continuam de pé: os
> **92 °C medidos no painel** (R-65), o manuseio do U.FL, a análise de adesivos
> e o método de medir EMI. Tudo isso foi trazido para cá. O que ficou para trás
> foi a geometria.

---

## 1. A arquitetura, em uma página

```
                      ┌──────────────────────────────┐
   TAMPA  ──────────► │  lisa · furo do LED no centro│  ◄── voltada ao CÉU
   encaixe por atrito └──────────────────────────────┘
                       ╔══════════════════════════════╗
                       ║  antena 25×25 colada na PCB  ║ ◄── vista de céu
                       ║                              ║     pela tampa
   FACE FRONTAL ─────► ║ [enc] [ display ] [buzzer]   ║ ◄── voltada ao motorista
   com os recortes     ║                              ║
                       ║  PCB 120×120 em 4 pilares    ║
                       ╚══════════════════════════════╝
                          ▲                        ▲
                     ímã no fundo            TRASEIRA: cartão e USB
```

| Face | O que leva |
| :--- | :--- |
| **Frontal** | encoder · display · buzzer. Recorte do display pela **área visível** |
| **Superior** | tampa lisa, furo único do LED no centro. É por onde a antena vê o céu |
| **Traseira** | rasgo do cartão microSD e abertura do cabo do Pico |
| **Inferior** | adesivo magnético para o painel |

---

## 2. A caixa

Impressa, paramétrica em OpenSCAD. O modelo e as decisões de geometria estão
fora deste repositório, com a cadeia de geração.

| | |
| :--- | :--- |
| Externo | 130 × 130 × 67 mm |
| Parede e piso | 3 mm |
| Fechamento | batente interno, tampa desce por cima, **atrito — sem parafuso** |
| Folga do encaixe | 0,4 mm por lado |

### 🔴 O material é decisão térmica, não estética

O painel mediu **92 °C**. Isso elimina quase tudo:

| Material | Amolece a | No seu painel |
| :--- | :--- | :--- |
| PLA | ~60 °C | ❌ deforma, não é "talvez" |
| PETG | ~80 °C | ❌ escorre sob peso próprio |
| ABS | ~105 °C | 🟡 serve, mas amarela e fragiliza com UV |
| **ASA** | ~105 °C | ✅ mesma temperatura, com a química de UV resolvida |

⚠️ **Nada com carga de fibra de carbono.** É condutivo o bastante para atrapalhar
a antena, e a tampa é justamente por onde o patch enxerga o céu.

⚠️ **ASA exige câmara fechada.** Empena sem ela e libera estireno. Numa
impressora aberta o material disponível cai para PETG, que os 92 °C reprovam.

### A margem do ASA é apertada, e isso é consciente

105 °C de Tg contra 92 °C medidos são **13 °C**. Num dia pior a margem some.
A alternativa com folga de verdade seria nylon PA12 por MJF, com deflexão perto
de 175 °C — descartada por custo, mas é para onde ir se o ASA ceder.

**Pendência M-08** decide: um dia de registro térmico no painel, com o sensor
que já existe dentro do RP2350.

---

## 3. A placa

PCB fabricada de **120 × 120 mm**, apoiada em **quatro pilares de 5 mm** que
nascem do piso da caixa, nos furos de M3 das quinas.

Isso substitui todo o capítulo de chassi de placa perfurada da v1 — e com ele
somem os problemas que aquele capítulo existia para resolver: pernas cortadas
no lado de baixo, ilhas de cobre soltas, colagem com VHB sobre pontos de solda.

> ✅ **O ganho que não é óbvio:** com placa fabricada, a fiação deixou de ser
> transcrita à mão. Ela sai do `netlist.py`, e o `pcb.md` registra a verificação
> que compara a netlist do KiCad com a fonte, nó a nó.

### Os módulos sobem em barra de pinos

Decisão da placa, registrada no `pcb.md`: a PCB é uma **carrier board**, e Pico,
GPS, display, leitor e conversor plugam nela. O soquete levanta cada módulo
**8,1 mm** acima da placa.

Módulo com defeito troca puxando. E o U.FL do GPS nunca precisa ser tocado.

---

## 4. O GPS e a antena

Os dois na placa, vizinhos, na extremidade oposta ao conversor.

### 4.1 A antena é colada, e a área é proibida

Patch cerâmico de **25 × 25 mm**, colado com **fita espuma dupla face**.

A área sob ela é zona de exclusão no projeto da placa: **plano de terra sólido,
sem trilha atravessando**. E **nada por cima** — o patch enxerga o céu pela
tampa, que é plástica e portanto transparente a 1,5 GHz.

O retângulo tracejado na serigrafia marca o lugar, com os dizeres `ANTENA GPS` e
`colar aqui - nao usar`. A zona existe para a verificação de projeto; a
serigrafia existe para quem monta.

⚠️ **A fita precisa ser acrílica estruturada (tipo VHB), especificada para
90–120 °C.** Fita espuma comum não descola — ela **escorre devagar**, e a peça
desce ao longo de semanas.

⚠️ A espuma afasta a antena do plano de terra em 1 a 2 mm, o que altera o
diagrama de radiação. Não é fatal; é coisa a observar na primeira volta real,
comparando o número de satélites com o de hoje.

### 4.2 Manuseio do U.FL — continua valendo

O conector aguenta cerca de **30 ciclos** de encaixe (datasheet Hirose). A fêmea
no módulo mede ~2 mm.

- **Encaixar uma vez, na montagem.** Com módulo e antena na mesma placa, não há
  motivo para desconectar na manutenção.
- Para desencaixar, **puxar reto para cima**, nunca de lado nem pelo cabo.
  Ferramenta de extração, se houver, ou unha sob a saia do conector.
- Ao encaixar, **alinhar e pressionar reto**, com o estalo. Encaixe torto
  deforma a saia e mata o conector de uma vez.

---

## 5. A face frontal

Da esquerda para a direita: **encoder · display · buzzer**.

| Peça | Medida | Recorte na caixa |
| :--- | :--- | :--- |
| Display 2,4" | módulo 70 × 46,7 × 5,7 | **área visível 60,8 × 44** |
| Encoder KY-040 | corpo 19,3 × 26,5 × 9,6 | furo da bucha roscada |
| Buzzer SFM-27 | ⌀22,4, orelhas a 29,6 | 2 furos de ⌀2,6 + leque de furos de som |

**O recorte do display é a área visível, não a placa do módulo.** A moldura
preta sobra por trás e é ela que encosta na parede — é o que esconde a
tolerância do recorte.

Abaixo do display há **14 mm** de material, com o nome `Coruja GPS` em relevo, e
a silhueta da coruja na coluna do encoder.

### ⚠️ O display avança para dentro

São 70 × 46,7 mm pendurados atrás da parede frontal, invadindo cerca de 6 mm.
A borda frontal da placa, nessa profundidade, tem de ficar livre de peça alta —
e está: nos 10 mm frontais só há os dois furos de fixação.

A folga entre o topo da placa e a base do display é de **0,4 mm**. Foi para
ganhar margem real que a borda subiu de 10 para 14 mm.

### O eixo do encoder

20 mm livres acima do corpo. Descontando 3 mm de parede e ~2 mm de arruela e
porca, restam **15 mm** para o knob — folga confortável.

⚠️ O **diâmetro do canhão roscado** ainda não foi medido. Os 6,0 mm lidos por
dentro da porca **não decidem**: garra interna em porca dá o diâmetro menor da
rosca. Numa M7×0,75, típica do KY-040, o menor fica perto de 6,1 — ou seja, 6,0
medidos são compatíveis com bucha de 7. Ver **M-12**.

---

## 6. A traseira, e o LED

**Cartão e USB faceados**, cada um com seu vão. Isso fixa a orientação de dois
módulos na placa: a boca do cartão e o conector USB apontam para fora.

O **LED** sai pela face de cima, furo único e centralizado.

> 📌 Escolheu-se a traseira para os dois vãos porque a caixa é presa ao painel
> por **ímã** e sai inteira quando preciso atualizar o firmware. O acesso ao
> cabo deixou de ser operação frequente.

---

## 7. Fixação ao painel

**Adesivo magnético** no fundo. Para atualizar o firmware, desliga-se a
alimentação e leva-se o conjunto.

### ⚠️ Duas perdas que se somam a 92 °C

- **A retenção magnética cai com a temperatura.** Ferrite flexível perde fluxo
  de forma sensível entre 80 e 100 °C — não some, enfraquece.
- **O adesivo escorre**, pelo mesmo mecanismo da fita espuma.

Um aparelho que se solta do painel em movimento é **problema de segurança**, não
de acabamento.

> **Conferir com a caixa quente.** Deixar o conjunto montado num dia de sol e
> tentar deslocá-lo. Todo adesivo parece bom frio.

---

## 8. Isolamento elétrico

| Onde | Como | Por quê |
| :--- | :--- | :--- |
| Sob o módulo GPS | **Kapton** na face de baixo do módulo | isola o módulo do que passa na placa |
| Entre a antena e a PCB | a própria fita espuma | ela já é dielétrica |
| Fios do Q1 e do chicote | **Kapton**, e ancorados contra vibração | o transistor fica preso aos fios, não soldado em pilar |

> ❌ **Nunca colar placa direto com VHB.** A fita tem 1 a 2 mm e as pernas de
> solda são mais altas: a placa ficaria apoiada nas pontas de solda, que é o
> pior apoio possível. Com a PCB fabricada em pilares, o problema não se
> apresenta — mas a regra fica, porque o reflexo de colar volta sempre.

---

## 9. Adesivos: o que usar e o que não usar

Esta seção veio inteira da v1. A química não mudou com a arquitetura.

### 9.1 ❌ Silicone RTV automotivo

**Não usar em contato com a eletrônica.** A maioria dos RTV baratos, inclusive
os automotivos de vedação, é de **cura acetoxi**: libera **ácido acético** ao
curar. O vapor condensa nos terminais e nas juntas de solda e corrói cobre,
estanho e latão — chega a descolar a solda do fio. É falha retardada: funciona
bem, e meses depois aparece verdete nas ilhas e um intermitente que se caça no
software.

Agrava-se porque silicone cura **por umidade do ar, de fora para dentro**: um
cordão grosso sob um módulo fica muito tempo sem curar no meio, mantendo o ácido
preso encostado na placa.

**Teste:** apertar um pouco num papel e cheirar. **Cheiro forte de vinagre =
acetoxi.** Cura neutra (alcoxi ou oxima) solta álcool e tem cheiro fraco. Na
embalagem, procurar "cura neutra", "não corrosivo" ou "para eletrônica" — *não*
"automotivo" nem "alta temperatura", que nada dizem sobre a cura.

"Alta temperatura" é, aliás, a especificação errada: Pico, display, GPS e cartão
somam cerca de 2 W. Pagar por 300 °C não compra nada e costuma vir justo com a
química corrosiva, porque é silicone de junta de motor.

Uso aceitável: **na caixa, não na eletrônica** — vedar a tampa contra pó, ou
fazer o passa-cabo. E curar com a caixa vazia antes de pôr a placa.

### 9.2 ❌ Cola quente

O reflexo natural, e errado aqui. EVA amolece entre 60 e 80 °C, e painel ao sol
passa bem disso. A montagem desaba num estacionamento à tarde.

Quem esquenta não é o circuito — é o **carro**. Essa é a temperatura que
seleciona o adesivo.

### 9.3 ⚠️ Se um dia houver blindagem, não usar alumínio

- A maioria das fitas de alumínio tem **adesivo acrílico isolante**. Tiras
  sobrepostas **não** se conectam, e o resultado são ilhas isoladas em vez de um
  plano. É o erro prático mais comum do assunto.
- **Alumínio não solda** com estanho e fluxo comuns — o óxido não deixa.
  "Aterrada" exigiria conexão mecânica, parafuso com arruela mordendo a folha.

**Usar fita de cobre com adesivo condutivo:** solda normalmente, as sobreposições
conduzem, e o fio de terra se prende com um ponto de solda.

E dois cuidados que valem para qualquer folha:

- **Aterrar curto e em vários pontos**, no terra do **próprio módulo GPS**.
  Blindagem aterrada por um ponto só e com fio longo pode ser **pior que
  blindagem nenhuma**: a indutância do fio faz com que aquilo não seja terra na
  frequência do ruído, e a folha passa a ser antena ressonante que acopla ruído
  para dentro. Se o fio não puder ser curto, é melhor deixar a folha flutuando.
- **Isolar a face externa da folha.** Um plano de terra nu virado para as placas
  e a fiação é curto esperando a hora. A ordem é folha, depois isolante, e o
  isolante é que olha para o circuito.

> 📌 Com a PCB fabricada, a blindagem ficou **menos provável de ser necessária**:
> a antena ganhou plano de terra contínuo embaixo e está na extremidade oposta ao
> conversor chaveado. Continua contingente à medição — ver **M-06**.

### 9.4 ✅ Resumo

| Uso | Escolha |
| :--- | :--- |
| Antena na PCB | **fita acrílica estruturada**, 90–120 °C |
| Caixa no painel | **adesivo magnético**, conferido quente |
| Isolamento sob o módulo GPS | **Kapton** |
| Vedação da tampa / passa-cabo | silicone de **cura neutra**, curado com a caixa vazia |
| Blindagem, se necessária | **fita de cobre** com adesivo condutivo |

---

## 10. O que não pode ser coberto, em nenhuma hipótese

| Peça | Consequência de cobrir |
| :--- | :--- |
| **Furos de som do buzzer** | Abafa o volume. Levaria a procurar erro no PWM onde não há. |
| **Flex e vidro do display** | Ponto de fadiga no flex. Silicone na borda do bezel é aceitável; no flex, não. |
| **A área da tampa acima do patch** | Bloqueia o céu e inutiliza o GPS. |
| **A zona proibida da antena, na placa** | Trilha ou cobre ali degrada a recepção, e o sintoma — fix demorado ou instável — ninguém atribui ao desenho da placa. |
| Rasgo do cartão, abertura do USB, eixo do encoder | Por motivo óbvio. |

---

## 11. Pendências

Numeração própria (**M-NN**), para não colidir com os **R-NN** de
`revisao_tecnica.md`.

> 📌 **M-01 a M-05 da v1 foram encerradas sem resposta.** Todas mediam a Patola
> — área entre torres, altura das torres, parafuso que acompanha a caixa,
> orientação dos eixos. A caixa saiu do projeto e as perguntas com ela.

| Item | O que falta | Trava o quê |
| :--- | :--- | :--- |
| **M-06** | A/B de C/N0 pela GSV, USB × step-down | Decide se há blindagem. Menos provável que na v1, mas não descartada. |
| **M-07** | Para-brisa do veículo é atérmico? | Se for, muda o regime térmico inteiro. |
| **M-08** | **Campanha térmica:** um dia parado ao sol, na posição real, registrando a cada minuto | Fecha o **R-65** e decide se o ASA aguenta. O RP2350 tem sensor **no próprio die**, no canal 4 do ADC, e o firmware não usa o ADC para nada — há um termômetro ocioso dentro do aparelho e um `LoggerCartao` que já grava no cartão. ⚠️ O sensor lê a pastilha, que corre acima do ambiente por autoaquecimento: caracterizar o desvio uma vez em ambiente conhecido. |
| **M-09** | 🆕 **Folga do encaixe da tampa** | Imprimir só a tampa com 0,3 / 0,4 / 0,5 e escolher pelo tato. Meia hora cada. |
| **M-10** | 🆕 **Empenamento do ASA** numa peça de 130 mm | Apoiar numa superfície plana e ver se balança nos cantos. Não aparece em amostra pequena. |
| **M-11** | 🆕 **Retenção do ímã com a caixa quente** | Segurança. Testar num dia de sol, não frio. |
| **M-12** | 🆕 **Diâmetro do canhão roscado do encoder** | Única cota da caixa sem margem para menos. A porca não responde — ver §5. |
| **M-13** | 🆕 Furo do LED, rasgo do cartão, abertura do USB | Estimativas no modelo. As duas últimas foram alargadas de propósito; a do LED não. |
| **R-13** | Corrente agregada e temperatura do LDO do display | Aberto em `revisao_tecnica.md`; a caixa fechada torna a medição mais relevante, não menos. |

> ⚠️ **M-09, M-10, M-12 e M-13** são impressora e paquímetro, e saem rápido assim
> que a impressora chegar. **M-06, M-07, M-08 e M-11** exigem o veículo — e o
> M-08 exige um dia de sol, então vale deixar o registro pronto antes do próximo
> verão.
