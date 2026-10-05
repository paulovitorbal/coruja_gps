# 🔧 Placa do coruja_gps — projeto no KiCad

**Projeto de estudo.** Ferramenta: KiCad 10.0.6.
**Estado em 2026-10-05:** biblioteca de símbolos e esquemático gerados e
verificados. **A placa (`.kicad_pcb`) ainda não existe.**

---

## 1. De onde vem o desenho

```
bom_schematic.md          revisado por gente; é a fonte de verdade da fiação
      │
      ▼
netlist.py                PECAS, CONN, NETS — transcrição única
      │
      ├─────────────► gera_fritzing.py ──► .fzz          (desenho de protoboard)
      │
      ▼
kicad_mapa.py             tradução: referência, símbolo, footprint, pad
      │
      ├─► kicad_sym.py  ──► kicad/coruja.kicad_sym       (16 símbolos)
      └─► gera_kicad.py ──► kicad/coruja.kicad_sch       (21 peças)
                              │
                              ▼
                        verifica_kicad.py                 ⬅ a verificação que vale
```

**Nada é desenhado à mão.** Quem muda a fiação muda o `bom_schematic.md`, depois
o `netlist.py`, e regera. Duas transcrições da mesma netlist divergiriam, e
divergiriam em silêncio.

O `expressao_s.py`, usado pelos três geradores, é o leitor e escritor do
formato de arquivo do KiCad — s-expressão. Ele preserva quais átomos estavam
entre aspas, e isso não é detalhe: o KiCad distingue `passive` de `"passive"`,
e recusa a biblioteca inteira quando o que espera entre aspas vem nu.

### Os gabaritos 1:1 cumpriram o papel e foram removidos

Durante a medição dos módulos existiram folhas HTML em escala 1:1, geradas por
um script, para imprimir e pôr a peça em cima. Elas acharam coisa: confirmaram
o contorno do leitor microSD e do GPS, reprovaram os furos de um footprint de
terceiro, e mostraram que a peça real tem quinas arredondadas onde o arquivo
desenhava chanfro.

Foram apagadas em 2026-10-05, junto com o gerador, depois que todas as medidas
fecharam. Os comentários que dizem "confirmado no gabarito 1:1" pelo código e
pelo `medidas.py` continuam verdadeiros — referem-se ao que foi feito, não a um
arquivo que ainda exista. **Não procure por eles.**

Se algum módulo novo entrar no projeto, vale recriar: um gabarito impresso custa
uma folha de papel e responde o que o paquímetro sozinho não responde, que é se
a geometria inteira fecha de uma vez.

### Reproduzir

```sh
cd dispositivo
python3 kicad_sym.py        # biblioteca de símbolos
python3 gera_kicad.py       # esquemático, projeto e tabelas de biblioteca
python3 verifica_kicad.py   # confere contra o netlist.py — sai 0 se bate
```

---

## 2. A verificação que vale

As outras provam que o arquivo é **bem formado**: o símbolo carrega, o
esquemático abre, o ERC passa. Nenhuma delas prova que está **certo**.

O `verifica_kicad.py` faz o próprio KiCad exportar a netlist dele e compara nó
a nó com o `netlist.py`:

```
fonte: 31 redes / 83 nos
kicad: 31 redes / 83 nos

OK — o esquematico liga exatamente o que o netlist.py manda.
```

A classe de erro que isso pega é a pior do projeto: **ligação trocada que não
gera erro nenhum**. O esquemático fecha, o KiCad não reclama, a placa é
fabricada, e o defeito aparece na bancada.

### Prova de que a verificação é sensível

Um teste que passa de primeira não prova nada. Mutante aplicado em
`pad_do_pico()`, removendo o deslocamento de 20 pinos entre os dois headers do
Pico:

```
SPI0_MOSI: nó da fonte ausente no esquematico: ('J2', 25)
SPI0_MOSI: nó a mais no esquematico:           ('J2', 5)
SPI0_SCK:  nó da fonte ausente no esquematico: ('J2', 24)
...
```

Morto. É o erro exato que o `kicad_mapa.py` avisa que "troca metade das
ligações **sem gerar erro**".

---

## 3. Os defeitos que apareceram ao gerar

Registrados porque todos são do tipo que passa despercebido.

| # | Defeito | Como apareceu |
| :-- | :--- | :--- |
| 1 | Footprint de resistor com nome errado (faltava `_Horizontal`) | conferência de existência de cada footprint na biblioteca |
| 2 | Escritor de s-expressão tirava as aspas das strings | KiCad recusava a biblioteca com "não foi possível carregar", sem dizer onde |
| 3 | `(offset ...)` dentro de `pin_numbers`, que só aceita `hide` | idem — mesma mensagem genérica |
| 4 | **O Pico inteiro sem ligação** no esquemático | `verifica_kicad.py`; o KiCad achou o arquivo perfeito |
| 5 | Símbolo compartilhado com descrição diferente entre biblioteca e esquemático | ERC: `lib_symbol_mismatch` |

O **#4** é o que justifica todo o resto. A netlist chama a peça de `PICO`; o
esquemático a divide em `PICO_A` e `PICO_B`. Procurar a rede pelo nome errado
não dá erro — devolve "pino sem rede", e os 40 pinos do Pico saíram sem
rótulo. O arquivo estava bem formado, o ERC passava, e a placa não teria
microcontrolador ligado em nada.

---

## 4. Decisões de desenho

### 4.0 O Pico é uma peça só, com o footprint oficial

`Module:RaspberryPi_Pico_Common_THT`: 40 pads numerados de 1 a 40, iguais ao
pino físico, nas duas colunas em x=0 e x=17,78 mm.

Já foi feito com **duas barras de 20 pinos**, e era pior por dois motivos. O
primeiro é o deslocamento: o segundo header recomeça do pad 1, então o físico
21 virava o pad 1 de `J2` — errar isso troca metade das ligações sem gerar
erro. O segundo não tinha conserto por verificação: **a distância entre as
fileiras não estava escrita em lugar nenhum**, e dois footprints independentes
podem ser posicionados a qualquer distância. A placa voltaria da fabricação
com os furos errados e o Pico não entraria.

Trocar duas peças por uma apagou uma classe inteira de erro em vez de
verificá-la.

### 4.1 Tudo em barra de pinos

Decisão de 2026-10-05. A placa é uma **carrier board**: Pico, GPS, display,
leitor microSD, encoder e conversor plugam nela. Soldados ficam só os passivos,
a proteção de entrada e os conectores.

É o que mantém o aparelho serviçável — módulo com defeito troca puxando, e o
U.FL do GPS (30 ciclos de encaixe, R-67) nunca precisa ser tocado.

### 4.2 Ligação por rótulo de rede, não por fio

Cada pino ganha um toco de 2,54 mm e um rótulo. Para o KiCad, dois rótulos
iguais são a mesma rede — tão ligado quanto um fio.

Rotear 83 nós automaticamente produz um emaranhado que ninguém revisa. Como o
esquemático existe para ser conferido por gente, legibilidade vale mais que
ortodoxia de desenho. Efeito colateral bom: **a posição dos símbolos não tem
significado elétrico**, então arrumar o desenho nunca muda a placa.

### 4.3 Biblioteca de símbolos própria, footprints da biblioteca padrão

Símbolo é desenho: nome muda entre versões do KiCad, e um projeto que
referencia `Device:R` abre numa máquina e não abre na outra. Por isso a
biblioteca vai junto.

Footprint é geometria de fabricação, que não convém reinventar — e os nomes
daquela biblioteca são estáveis. O `verifica_kicad.py` confere que cada um
existe antes de qualquer outra coisa.

### 4.4 Um símbolo por peça, com nome de pino de verdade

`VCC 5V / RX / TX / GND` em vez de `1 2 3 4`. Um conector numerado não deixa
ninguém perceber RX trocado com TX.

Nos dois headers do Pico, o **nome** é o pino físico (1 a 40) e o **número** é o
pad do KiCad (1 a 20 em cada). Os dois ficam à vista de propósito: é onde o
deslocamento de 20 fica visível para quem revisa.

### 4.5 UUID determinístico

Gerados ao acaso, cada execução produziria um arquivo inteiro diferente e o
`git diff` seria inútil. Saem de `uuid5` sobre o nome do objeto.

---

## 5. Polaridades — verificadas, não presumidas

Lidas dos próprios arquivos da biblioteca em 2026-10-05.

| Peça | Conclusão | Como foi verificado |
| :--- | :--- | :--- |
| **C1, C5** eletrolíticos | pad 1 é o **positivo** | o `+` não é texto, é desenho: duas linhas de 1,00 mm cruzando em (−2,48; −2,88), do lado do pad 1 (x=0) e oposto ao pad 2 (x=5) |
| **D1** Schottky | pad 1 é o **catodo** | o texto `K` do footprint está em x=0, posição do pad 1 |
| **Q1** TO-92 | `E=1, B=2, C=3` | contorno com a **reta** em y=+1,75 (lado chato) e arcos em y=−2,48; quem olha a face chata vê o pad 1 à esquerda — a mesma vista da medição de 2026-10-04 |
| **TVS1** | **sem polaridade** | propriedade da peça: P6KE24**CA** é bidirecional (BOM 27) |

### ⚠️ A serigrafia do TVS mente

O footprint `D_DO-201AD` desenha um `K` em x=0, marcando o pad 1 como catodo.
Para o `CA` bidirecional essa marca **não significa nada**, e a peça real não
tem banda correspondente.

Fica registrado para que ninguém olhe a serigrafia, procure a banda, não ache e
conclua que recebeu a peça errada. Se incomodar na montagem, o conserto é um
footprint próprio sem a marca — não trocar a peça.

---

## 6. Estado da verificação

| Verificação | Resultado |
| :--- | :--- |
| Footprints existem na biblioteca | 21/21 |
| Biblioteca de símbolos carrega e renderiza | 16/16 |
| Ida-e-volta do leitor/escritor de s-expressão | 300/300 arquivos reais do KiCad, idênticos |
| ERC | **0 violações** |
| Netlist do KiCad × `netlist.py` | **31 redes / 83 nós, idênticos** |
| Mutante do deslocamento do Pico | morto |
| Geometria: sobreposição e limites da folha | nenhuma; 166 × 258 mm numa A3 |

Os 21 pinos deliberadamente sem uso (pads livres do Pico, o `n/c` da entrada de
12 V e `D1`/`DAT2`/`DET` do leitor microSD) levam marca `no_connect`. Sem ela o
ERC acusaria 21 avisos legítimos, e 21 avisos legítimos escondem o ilegítimo
que aparecer depois.

---

## 7. Busca por footprint oficial das demais peças

Feita em 2026-10-05, na biblioteca do KiCad 10.0.6, depois que o Pico mostrou o
ganho. **O Pico foi a exceção, não a regra.**

| Peça | Existe footprint oficial? | O que foi usado, e por quê |
| :--- | :--- | :--- |
| **Pico 2 W** | ✅ `Module:RaspberryPi_Pico_Common_THT` | adotado — é produto padronizado |
| GPS GY-GPS6MV2 | ❌ | `RF_GPS` só tem **módulo nu** (`ublox_SAM-M8Q`, `ORG1510`), não a placa de breakout. Barra de 1×4 |
| Leitor microSD | ❌ | `Connector_Card` só tem **soquete nu** (Hirose DM3AT, Molex). O nosso é breakout com regulador. Barra de 1×9 |
| Display ST7789V | ❌ | nada para a placa de breakout. Barra de 1×8 |
| Encoder KY-040 | ❌ | `Rotary_Encoder` só tem **encoder nu** (Alps EC11E, Bourns PEC12R). O KY-040 é módulo com pull-ups. Barra de 1×5 |
| Conversor LM2596 | ❌ | nada. Barra de 1×4 |
| LED RGB 10 mm | ⚠️ parcial | há `LED_D5.0mm-4_RGB` (5 mm) e `LED_D10.0mm-3` (10 mm, 3 pernas), **não** 10 mm com 4. Irrelevante: é peça de painel, na placa é conector |
| Buzzer SFM-27 | ⚠️ parcial | `Buzzer_Beeper` tem outros modelos. Irrelevante pelo mesmo motivo: vai por fio |
| Fusível, TVS, Schottky, Q1, C, R, entrada 12 V | ✅ | já usam footprint específico |

**Por que quase nada tem:** o KiCad traz footprint de *componente*, e cinco das
nossas peças são *placas de breakout* genéricas, sem padrão de fabricante. Para
elas a barra de pinos é a representação correta — é literalmente o que vai
soldado na placa.

### ⚠️ O que a barra genérica não carrega

Um `PinHeader_1x08` tem os furos e **nada mais**: sem contorno do módulo, sem
área de ocupação. Na hora de posicionar, nada impede que o display fique por
cima do GPS — a verificação de projeto não acusa, porque para ela só existem
oito furos.

O conserto é footprint próprio, com o contorno medido de cada módulo. Isso
exige **paquímetro**, não biblioteca, e está na lista abaixo.

### ⚠️ Uma pergunta mecânica em aberto

O `montagem.md` põe **display e encoder na face frontal**, voltados ao
motorista. Se os dois são fixados na caixa e não na placa, a barra de pinos
deixa de ser "onde o módulo encaixa" e passa a ser "conector para o chicote" —
muda o tipo de peça e muda o desenho. **Decisão pendente**, e ela precede o
contorno da placa.

---

## 7.5 Dois pontos que quem montar precisa saber

Ambos são consequência do roteamento feito à mão em 2026-10-05 e **não aparecem
em lugar nenhum além do arquivo da placa**.

### ⚠️ O pino 1 do J3 tem conexão sólida com o terra

O conector do display é o único ponto da placa sem alívio térmico. Ele foi feito
assim de propósito: naquela posição, o pad é o primeiro de uma fila de oito a
2,54 mm, com o vizinho em outra rede, e não sobrava cobre para os dois raios
térmicos que a verificação exige.

**Na prática:** aquele pino suga calor do plano inteiro. Soldá-lo exige ferro
com reserva térmica e mais tempo que os demais. Se a solda não molhar, o
problema é esse — não é falta de fluxo nem pad oxidado.

Os outros sete pinos do mesmo conector continuam com alívio térmico normal.

### A ilha de terra do GPS é costurada por uma trilha

O pad de terra do módulo GPS (J1, pino 4) ficou numa ilha de cobre de `F.Cu`
separada do plano. Uma trilha de 11,6 mm leva até uma via em (101,0; 81,5), que
fecha no plano de `B.Cu`.

Funciona e foi verificado, mas é mais frágil que um pad em plano contínuo:
**quem mexer no roteamento daquela região precisa preservar essa trilha**, ou o
terra do GPS volta a depender só da face de baixo.

---

## 7.6 Arquivos de fabricação

Não são versionados: saem inteiros do `.kicad_pcb`, que é. Regerar:

```sh
cd kicad
kicad-cli pcb export gerbers -o fabricacao/ --no-protel-ext coruja.kicad_pcb
kicad-cli pcb export drill   -o fabricacao/ --format excellon \
    --excellon-separate-th --generate-map --map-format gerberx2 coruja.kicad_pcb
kicad-cli pcb export pos     -o fabricacao/coruja-posicoes.csv \
    --format csv --units mm --side both coruja.kicad_pcb
```

**Apague as camadas vazias antes de enviar.** O KiCad gera 26 arquivos; 14 saem
vazios porque a placa é toda de furo passante e tem serigrafia só na face de
cima. Camada vazia no pacote faz o fabricante perguntar — ou cobrar por um
processo que não existe. O que se envia são 12:

```
F_Cu  B_Cu  F_Mask  B_Mask  F_Silkscreen  Edge_Cuts
PTH.drl  NPTH.drl  + os dois mapas de furação  + job  + posições
```

### Conferência do pacote, 2026-10-05

Arquivo gerado não é arquivo correto. O pacote foi comparado com a placa:

| | |
| :--- | :--- |
| Furos PTH | **155 na placa, 155 no arquivo**, idênticos diâmetro a diâmetro |
| Furos NPTH | **9 e 9**, idem |
| Contorno no Gerber | **120,00 × 120,00 mm** |
| Posições | 21 peças |

Os 9 NPTH se explicam: 2 do leitor microSD (M2.5), 4 do GPS (M2.5), 2 do
conversor (M3) e **1 do porta-fusível**, que tem fixação própria — este último
surpreendeu na conferência e foi conferido antes de ser aceito.

---

## 8. O que falta

1. **Desenhar a placa** (`.kicad_pcb`): contorno, posicionamento, roteamento.
   Antes disso é preciso decidir a dimensão, que depende da caixa — ver
   `montagem.md`.
2. **Medir os cinco módulos a paquímetro** — GPS, leitor microSD, display,
   encoder e conversor. Falta o passo real (2,54 mm é presumido pela foto) e,
   principalmente, o contorno, que é o que vai virar footprint próprio e
   impedir que dois módulos ocupem o mesmo espaço.
3. **Largura de trilha da entrada de 12 V.** O resto é sinal, mas essa trilha
   carrega a corrente toda.
4. **DRC** depois que a placa existir.
5. Reavaliar se vale fabricar: ver ADR 0011 — monta-se primeiro com o que já
   existe, e mitigação só depois de medir.

> 📌 Nada aqui foi fabricado nem validado em bancada. Os números desta página
> saem de arquivo e de ferramenta, não de instrumento.
