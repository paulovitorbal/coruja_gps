# 🔍 Revisão Técnica — Pontos que Necessitam Correção

**Projeto:** Detector de Radares GPS Inteligente (Raspberry Pi Pico 2 W)
**Documentos revisados:** `bom_schematic.md`, `requirements.md`
**Data da revisão:** 2026-09-15
**Contexto:** projeto pessoal de estudo — aperfeiçoamento de habilidades em embarcados.

## Sumário por severidade

> ✅ **2026-09-15 — Revisão 2 aplicada aos documentos de origem.** `requirements.md` e
> `bom_schematic.md` foram atualizados com todos os itens abaixo. Os originais estão em
> `requirements.md.bak` e `bom_schematic.md.bak`. O que resta não é mais *documentar* —
> é **implementar, medir e confirmar as suposições**.

| Severidade | Documentado | Pendente de bancada | Significado |
| :--- | :---: | :---: | :--- |
| 🔴 **Bloqueador** | 8 de 8 | 1 medição (R-05) | Queima componente, ou o requisito não roda no hardware. R-06, R-14 e R-21 fechados. |
| 🟠 **Relevante** | 22 de 22 | 2 medições (R-13, R-17) + 2 inspeções (R-14, display) | Circuito liga, comportamento sai errado ou falha em silêncio. **R-22 a R-26**. |
| 🟡 **Lacuna** | 9 de 9 | — | Requisito que não existia. |
| ⚪ **Editorial** | 5 de 5 | — | Erro de texto ou numeração. |

### ⏳ O que continua aberto

**📐 Medições de bancada** — as únicas coisas que exigem o hardware na mão:

| Item | O que medir | Por quê |
| :--- | :--- | :--- |
| **R-05** | Vf e corrente reais dos LEDs verde **e azul**, mais **calibração de razão por PWM** | **Pré-requisito do R-19 (amarelo) e do RF03.9 (rosa).** Sem verde não há amarelo; sem azul não há rosa, e o estado de margem apareceria como vermelho piscante — indistinguível do Perigo. Calibrar as razões R:G e R:B e verificar as cores lado a lado, em luz ambiente e sob sol direto. |
| **R-13** | Corrente agregada no `3V3_OUT` com backlight em 100% | Com o GPS migrado para 5 V (R-14), sobraram display e SD — margem provavelmente confortável, mas não verificada. |
| **R-17** | Consumo do SFM-27 e pinagem do BC337/2N2222 | A ordem E-B-C difere da do BC547. |

**🔍 Inspeção antes de energizar:**

| Item | Verificação |
| :--- | :--- |
| **R-14** | Confirmar o encapsulamento de 3 pinos junto ao VCC da placa GPS. Sem regulador, os 5 V destroem o módulo. 30 segundos. |

**❓ Decisões de produto — não bloqueiam nada:**

| Item | Decisão |
| :--- | :--- |

**🔜 Escopo futuro, fora desta versão:**

| Item | Escopo |
| :--- | :--- |
| **RF03.10** | Controle de velocidade média em trecho. Desenho completo registrado. |
| **Telas e interfaces visuais** | Sessão dedicada. Inclui a distinção visual de radar móvel do RF03.5 e todos os estados que migraram para a tela no R-25. |

**🔨 Todo o resto é implementar.** Ver ordem de ataque abaixo.

### ✅ Decisões fechadas pelo autor

| Item | Decisão | Data |
| :--- | :--- | :--- |
| **RF03.6** | **Limiar de multa com margem de segurança:** desconto de projeto de 6 km/h (≤100) e 5% (>100), contra os valores legais de 7 km/h e 7% confirmados na Resolução CONTRAN. | 2026-09-15 |
| **RF03.7** | **Buzzer escalonado em 3 faixas** na Zona de Perigo (lento / rápido / contínuo). | 2026-09-15 |
| **RF03.9** | **Rosa piscante a 1 Hz** entre o limite da via e `V_infra` — silencioso. Rosa (R+B) em vez de laranja (R+G): separação por canal, não por razão. | 2026-09-15 |
| **RF03.4** | **Precedência: Perigo > Margem > Semáforo > Conforme.** | 2026-09-15 |
| **RF03.5** | **`TYPE=5` como radar simples** + sinal de alerta na tela. Média diferida. | 2026-09-15 |
| **RF06.1** | **Categorias exportadas: `1,2,4,5`** (Radar Fixo, Radar Móvel, Semáforo c/ Câmera, Semáforo c/ Radar). Fora do escopo: Polícia Rodoviária, Pedágio e **Lombada** — esta última, com 19.351 pontos, levaria a base a 37.645 registros e estouraria o RNF07. | 2026-09-16 |
| **R-25** | **LED RGB exclusivo de estado de via** — boot, sem sinal, OTA e falha de dados só na tela. **LED verde de Wi-Fi removido do projeto**; LED RGB é o único indicador luminoso. | 2026-09-15 |
| **RF03.8** | **Zona de Aproximação silenciosa** — buzzer exclusivo da Zona de Perigo. | 2026-09-15 |
| **L-08** | **C++17 com Pico SDK** — dual core real e orçamento de memória. Teste de heap do MicroPython dispensado. | 2026-09-15 |
| **R-06** | **Conector JST-XH** de 2 vias no lugar do Jack P2. | 2026-09-15 |
| **R-14** | **GPS alimentado em 5 V** — placa GY-GPSV3-NEO M8N tem LDO embarcado. | 2026-09-15 |
| **R-19** | **Zona de Semáforo silenciosa**, LED amarelo/vermelho alternando a 2 Hz. | 2026-09-15 |
| **R-21** | **GPS+GLONASS a 4 Hz nominal, piso de 3 Hz.** Gerou o RF01.5. | 2026-09-15 |

### ⚠️ Correções de pinagem física (2026-09-17)

Com os módulos em mãos, duas pinagens dos documentos estavam erradas. Ambas vieram de
inferência minha, não de leitura da peça.

| Módulo | Documento (rev. 2) | Placa física | Consequência se soldado |
| :--- | :--- | :--- | :--- |
| **KY-040** | `CLK DT SW + GND` | **`GND + SW DT CLK`** | **ordem invertida** — 3,3 V e GND direto em dois GPIO |
| **Leitor SD** | 6 pinos `VCC GND CLK DI DO CS` | **8 pinos** `3V GND CLK DO/SO CMD/SI D3/CS DAT2 DET` | rótulos inventados; 2 pinos inexistentes no meu mapa |

Lição registrada: rótulo de serigrafia é dado, não dedução. Os documentos passaram a
usar os nomes impressos na placa, porque na bancada se lê a placa.

### 🆕 Card detect aproveitado (2026-09-17)

O pino `DET` do leitor SD, que eu nem sabia existir, foi ligado ao **GPIO 14** — livre
desde que o LED de Wi-Fi saiu (R-25). Permite ao RF07 distinguir **cartão ausente**, que
o motorista resolve inserindo o cartão, de **cartão ilegível**, que ele não resolve
dirigindo. Antes eram a mesma mensagem.

Custo zero em componentes: **a própria placa traz o pull-up de 4,7 kΩ para 3 V**, então
o GPIO 14 é entrada **sem pull** — o interno do Pico, que eu havia recomendado, seria
redundante e mais fraco.

A polaridade **não é incógnita de bancada**: o guia do fabricante documenta que o `DET`
fica em GND sem cartão e sobe a 3 V quando há cartão, logo **cartão presente = nível
ALTO**. Fica uma ressalva no RNF03: o `DET` é **indício, não autoridade** — a tentativa
de leitura continua sendo o veredito.

### Histórico de resoluções

| Data | Itens | Documento de solução | Observação |
| :--- | :--- | :--- | :--- |
| 2026-09-15 | **R-02** | `formato_dados.md` §1 | Premissa corrigida: base real tem 18.294 pontos, não 40–60 mil. Cabe em RAM; particionamento desnecessário. |
| 2026-09-15 | **R-03** | `formato_dados.md` §4 | Array ordenado por latitude + busca binária + pré-filtro inteiro. Custo medido: máx. 24 Haversines/ciclo. |
| 2026-09-15 | **R-18** | `formato_dados.md` §0 + `converte.py` | Layout confirmado por cabeçalho do arquivo; `DirType` confirmado por análise geométrica; conversor escrito e verificado. |
| 2026-09-15 | **R-19** *(parcial)* | `formato_dados.md` §7.1 | Comportamento decidido pelo autor: Zona de Semáforo silenciosa, LED amarelo/vermelho a 2 Hz. Implementação pendente e dependente do R-05. |
| 2026-09-15 | **L-08** | `requirements.md` RNF06 | **C++17 com Pico SDK decidido pelo autor.** Dual core real e orçamento de memória. Teste de heap do MicroPython dispensado. |
| 2026-09-15 | **TODOS** | `requirements.md` rev. 2 + `bom_schematic.md` rev. 2 | Os 34 itens aplicados aos documentos de origem, com tabelas de rastreabilidade em cada um. Originais preservados em `.bak`. |
| 2026-09-15 | **R-06** | `bom_schematic.md` §5 + `requirements.md` RNF05 | **Conector JST-XH confirmado pelo autor** em lugar do Jack P2. Suposição fechada. |
| 2026-09-15 | **R-14** *(reformulado)* | `bom_schematic.md` §3 | Faixa do chip (2,7–3,6 V) informada pelo autor revelou que a suposição de 3,3 V era mal fundamentada. Virou árvore de decisão por regulador da placa. |
| 2026-09-15 | **R-14** | `bom_schematic.md` §3 | **5 V decidido.** Placa identificada como GY-GPSV3-NEO M8N, família com LDO embarcado — alimentar em 5 V é a configuração de projeto da placa, funciona com qualquer LDO e alivia ~82 mA do regulador do Pico. |
| 2026-09-15 | **R-21, R-22** *(novos)* | `revisao_tecnica.md` | Datasheet oficial UBX-15031086 analisado. Teto de 5 Hz em GPS+GLONASS e faixa de entrada dos GPIO do GPS. Correntes, temperatura e config padrão agora ancoradas em datasheet. |
| 2026-09-16 | **R-20** *(resolvido)* e **R-26** | `formato_dados.md` §0.2 · `requirements.md` RF03.5/RF03.10 | Significado real dos códigos `TYPE` obtido da fonte da base: as contagens batem na unidade. **Minhas duas inferências de rótulo estavam erradas** — `TYPE=2` é semáforo com radar e `TYPE=5` é radar móvel. RF03.10 anulado por falta de dados. |
| 2026-09-15 | **R-20** *(corrigido)* + **R-25** *(novo)* | `requirements.md` RF03.5, RF03.9, RF03.10 | Teste de pareamento de `TYPE=5` refeito após erro de método meu — evidência é mista, não negativa. Requisito mantido pelo autor; média diferida por ausência de trechos no DF (5 pontos). Paleta do LED reduzida de 9 para 5 estados. |
| 2026-09-15 | **R-23** *(novo)* | `requirements.md` RF03.6 a RF03.8 | Tolerância legal e buzzer escalonado especificados pelo autor. A ancoragem literal dos percentuais no limite da via deixaria as faixas vazias em 82,5% dos radares; reancorada em `V_infra`. Aproximação silenciada. |
| 2026-09-15 | **R-21** | `requirements.md` RF01.4 + RF01.5 | **4 Hz nominal com piso de 3 Hz confirmado pelo autor.** A banda de tolerância gerou requisito novo de monitoramento da taxa efetiva — que serve primariamente como verificação de que a configuração UBX do RF01.2 foi aplicada. |

## Legenda de confiança da evidência

| Marca | Origem |
| :--- | :--- |
| ✅ **Verificado** | Confirmado contra pinout oficial do Raspberry Pi Pico 2 / formato de dados / especificação citada. |
| ⚠️ **Valor típico** | Valor típico de datasheet. **Medir com a peça real** — e aqui medir é parte do exercício: multímetro no LED e no buzzer ensina mais que aceitar o número. |
| ❓ **A confirmar** | Precisa de consulta ao datasheet do fornecedor específico. |

## Ordem sugerida de ataque

Os requisitos já estão escritos. Esta é a ordem de **construção**, priorizada por valor
didático e por dependência:

1. **Bancada, antes de soldar** — as três medições pendentes (R-05, R-13, R-17). São
   rápidas, se resolvem com multímetro e datasheet, e custam componente queimado se
   descobertas depois. O R-05 é o mais urgente: sem o canal verde funcionando, a Zona de
   Semáforo que você decidiu não tem como ser validada.
2. **Fechar as duas decisões de produto restantes** — RF03.4 (prioridade entre alertas
   simultâneos) e RF03.5 (distinção visual de radar móvel). Podem esperar o aparelho
   rodando; não bloqueiam nada. *(R-06, conector JST-XH, confirmado em 2026-09-15.)*
3. **Parser NMEA e matemática geográfica** (RF01, RF02) — o núcleo, e o melhor terreno
   para os testes do RNF10: vetores conhecidos, zero hardware na mesa. O wraparound
   355°/5° e os três valores de `DirType` são os casos de teste obrigatórios. O
   `radares.bin` já gerado serve de *fixture*.
4. **Carregador da base e busca em dois estágios** (RF06, RF02.1) — continua sendo a
   parte mais interessante do projeto: é onde restrição real de memória e CPU força
   decisões de estrutura de dados. Especificação pronta em `formato_dados.md` §2 e §4.
5. **Máquina de estados de zona** (RF03, RF07) — as quatro zonas, histerese, condição de
   aproximação, prioridade entre alertas e degradação segura. É aqui que a Zona de
   Semáforo vive.
6. **Concorrência entre núcleos** (RNF04) — divisão core 0 / core 1 e mutex do SPI0.
   Deixar para depois de a lógica estar correta em um único núcleo.
7. **OTA** (RF05) — por último: subsistema mais independente e o projeto já é útil sem
   ele. Escrita atômica e validação já especificadas.

---

---

# 🔴 Bloqueadores

## R-01 — `VBUS` não é o pino 39, e não deve ser o nó de entrada

- **Onde:** `bom_schematic.md` seções 1 e 5; `requirements.md` RNF01
- **Confiança:** ✅ Verificado (pinout oficial Pico / Pico 2)

**Problema.** O documento roteia o carregador veicular e os dois capacitores de filtro
para "VBUS (Pino 39)". No pinout real: pino 39 = `VSYS`, pino 40 = `VBUS`, pino 38 = `GND`.
O rótulo está trocado. Além do erro de nome, `VBUS` é ligado diretamente ao conector
USB do Pico — injetar 5V externo nesse nó cria conflito de fontes se o USB for plugado
para debug ou regravação.

**Correção.** Entrada 5V → diodo Schottky em série → `VSYS` (pino 39), com o par de
capacitores nesse nó, o mais próximo possível fisicamente dos pinos 39/38.

**Impacto propagado.** Atualizar também:
- Seção 5 do BOM: a linha de 5V do Jack do buzzer passa a vir de `VSYS` (ou do 5V
  pré-regulador, antes do Schottky).
- RNF01: trocar a menção a `VBUS` pelo nó correto.
- Seção 3 do BOM: a opção de alimentar o GPS em 5V muda de nó.

---

## ~~R-02 — A base de radares não cabe na SRAM~~ → ✅ **RESOLVIDO: premissa corrigida**

- **Onde:** `requirements.md` §5 (tela de boot: "Lê o SD e carrega dados na RAM")
- **Solução registrada em:** `formato_dados.md` §1
- **Resolvido em:** 2026-09-15

**Este achado estava errado.** Eu o levantei sobre uma estimativa de 40–60 mil pontos
na base brasileira. O número real, informado pelo autor, é de **18.294 pontos** — 3×
menor. Recalculando:

```
18.294 registros × 12 bytes = 214 KB   de 520 KB de SRAM
```

**A base inteira cabe na RAM.** O requisito original do §5 ("Lê o SD e carrega dados na
RAM") estava correto como escrito, e o particionamento em quadrantes que eu recomendei
é complexidade desnecessária: índice espacial, margem sobreposta, prefetch, histerese
de fronteira e duplo buffer são cinco mecanismos que não precisam existir.

Ganhos colaterais da carga integral: **nenhum acesso a SD com o veículo em movimento**,
e falha de cartão em movimento passa a ser irrelevante.

**O que sobrou de real deste achado** — dois pontos que valem independentemente:

1. **O framebuffer do display consome 150 KB** (320 × 240 × 2 B), quase o que a base
   inteira consome, e não aparecia em nenhum documento. É o maior alvo de otimização se a
   memória apertar (renderização em bandas libera **125 KB**). Ver `formato_dados.md` §6.
   *Número atualizado em 2026-09-17: o painel passou de 1,3" 240×240 para 2,4" 320×240,
   e com ele o framebuffer de 112,5 para 150 KB.*
2. **O orçamento de memória continua precisando ser escrito** como requisito, agora com
   números reais em vez de estimativas. Ver **L-02**.

O desenho de particionamento está preservado em `formato_dados.md` Anexo A, como plano
B para o caso de a base passar de ~35.000 registros.

---

## ~~R-03 — Haversine contra a base inteira a 5 Hz~~ → ✅ **RESOLVIDO**

- **Onde:** `requirements.md` RF02
- **Solução registrada em:** `formato_dados.md` §4
- **Resolvido em:** 2026-09-15

O achado era válido: 18.294 pontos × 5 Hz × Haversine em dupla precisão não executa em
tempo real. A solução não dependia do particionamento e sobreviveu à revisão do R-02.

**Solução: array ordenado por latitude + busca em dois estágios.**

1. **Busca binária** pela faixa de latitude `[lat − 300 m, lat + 300 m]` — ~15
   comparações de inteiros.
2. **Varredura da faixa** com descarte por longitude em aritmética inteira, sem
   trigonometria.
3. **Haversine apenas nos sobreviventes** — unidades por ciclo, não milhares.

Ordenar por **latitude** e não longitude é deliberado: o grau de latitude mede 111,32 km
em qualquer ponto do globo, então o limiar da busca binária é constante. Em longitude ele
dependeria de cos(lat) e o índice ficaria distorcido de norte a sul do país.

A invariante de ordenação é validada no carregamento (ver R-10 e `formato_dados.md` §2):
se o arquivo vier desordenado, a busca binária erra silenciosamente.

---

## R-04 — Filtro de rumo com bug de wraparound em 0°/360°

- **Onde:** `requirements.md` RF02, "Filtro de Sentido (Rumo)"
- **Confiança:** ✅ Verificado (aritmética circular + dados reais da base)
- **Código pronto em:** `formato_dados.md` §3.2

**Problema.** O requisito diz "se a diferença absoluta for maior que 30°, o radar deve
ser ignorado". Entre rumo 355° e radar 5° a diferença aritmética é 350° → o radar
**correto** é descartado. São três defeitos no mesmo requisito:

1. **Wraparound.** Usar diferença circular: `min(|a−b|, 360−|a−b|)`.
2. **Rumo inválido a baixa velocidade.** O azimute do NEO-M8N é ruído aleatório com o
   veículo parado ou abaixo de ~5 km/h. O filtro precisa de porta de velocidade — e
   abaixo do limiar ele deve **abrir**, não fechar: descartar por um rumo que é ruído
   perderia radares reais.
3. **O requisito só contempla o caso unidirecional**, que é 83,2% da base. Os outros
   dois casos, confirmados nos dados (ver R-18), precisam de tratamento próprio:
   - **`DirType=0`, omnidirecional — 1.419 registros (7,8%):** nunca podem ser
     descartados pelo filtro de rumo.
   - **`DirType=2`, bidirecional — 1.657 registros (9,1%):** válidos no rumo indicado
     **e no oposto**. Resolve-se com uma diferença circular dobrada em 0–90° em vez de
     0–180°, sem caso especial no laço.

> ⚠️ **Correção de uma suposição minha anterior:** eu havia afirmado que o sentinela de
> omnidirecional seria `Direction = 0`. **Está errado** — o `Direction` vem preenchido
> mesmo quando `DirType = 0`. O sentinela é o campo `DirType`. Se você tinha anotado a
> versão anterior, descarte.

---

## R-05 — 330 Ω deixa o verde e o azul do LED RGB praticamente invisíveis

- **Onde:** `bom_schematic.md` itens 11 e 8, seção 4
- **Confiança:** ⚠️ Valor típico (Vf depende do LED adquirido — **medir**)

**Problema.** LEDs difusos verde e azul têm Vf típico de 3,0–3,2 V. Com GPIO em 3,3 V e
330 Ω em série, sobram ~0,1–0,3 V no resistor → menos de 1 mA → emissão desprezível.
Como o LED RGB é o canal de alerta periférico (verde/amarelo/vermelho da matriz IHM),
dois dos três estados ficariam ilegíveis. O vermelho (Vf ≈ 2,0 V) fica em ~4 mA e funciona.

> 🔗 **O R-19 depende deste item.** A Zona de Semáforo decidida em 2026-09-15 usa
> alternância **amarelo/vermelho**, e amarelo é mistura de vermelho + verde. Com os
> 330 Ω atuais o verde não acende, então a alternância apareceria como **vermelho
> constante** — indistinguível da Zona de Perigo, o oposto exato da intenção. Este item
> deixa de ser ajuste estético e passa a ser **pré-requisito funcional**.

**Correção.**
- Vermelho: 330 Ω (manter).
- Verde e azul: 47–100 Ω.
- Equalizar as três cores por PWM em software depois da troca (o amarelo da Zona de
  Aproximação depende de mistura calibrada R+G).
- **Medir Vf e corrente reais** do LED específico antes de fechar o BOM.

---

## R-06 — O Jack J2 de 3,5 mm curto-circuita 5V no GND durante a inserção

- **Onde:** `bom_schematic.md` seção 5, itens 16 e 17; `requirements.md` RNF05
- **Confiança:** ✅ Verificado (geometria de conector P2)

**Problema.** Do jeito roteado (tip = 5V, sleeve = coletor do BC547), inserir ou remover
o plugue faz o sleeve varrer o tip. Se o transistor estiver conduzindo nesse instante,
resulta em curto direto do trilho de 5V para o GND, **sem o buzzer limitando a corrente** —
risco de dano ao transistor e ao carregador veicular. Conector P2/P3 de áudio para
energia é antipadrão conhecido justamente por isso.

**Correção (em ordem de preferência).**
1. Trocar por conector polarizado sem wiping — JST-XH ou Molex de 2 vias. *Recomendado:*
   preserva o requisito de liberação rápida do RNF05 sem o modo de falha.
2. Se o P2 for mandatório por restrição mecânica: inverter os nós (5V no sleeve) e
   adicionar PTC rearmável ou fusível na perna de 5V do jack.

---

## ~~R-21 — 5 Hz é exatamente o teto do NEO-M8N~~ → ✅ **RESOLVIDO**

- **Onde:** `requirements.md` RF01, RF01.4, RF01.5
- **Confiança:** ✅ **Verificado no datasheet oficial** (UBX-15031086, Tabela 1, p. 6)
- **Decisão do autor:** 2026-09-15 — **GPS+GLONASS a 4 Hz nominal, piso de 3 Hz**

> ✅ **Resolvido.** O autor optou por banda de tolerância em vez de ponto único, o que é
> mais robusto: 4 Hz nominal com 20% de folga sob o teto, e 3 Hz como piso aceitável.
> O custo do piso é de no máximo um período de amostragem de antecedência — 333 ms, ou
> 11 m dos 300 m de raio a 120 km/h.
>
> **Consequência que gerou requisito novo:** definir um piso só tem valor se o sistema
> souber quando o cruzou. Daí o **RF01.5** (monitoramento da taxa efetiva), que na
> prática funciona como verificação de que a configuração UBX do RF01.2 foi aplicada —
> taxa abaixo do nominal quase nunca é o receptor com dificuldade, é a configuração que
> não pegou.

**Problema.** O RF01 exige "taxa de amostragem mínima de 5 Hz". A Tabela 1 do datasheet
traz a taxa máxima de navegação do NEO-M8N por modo de GNSS:

| Modo GNSS | Taxa máx. | Precisão horiz. | Cold start | Sensib. (tracking) | Talker NMEA |
| :--- | :---: | :--- | :---: | :---: | :---: |
| **GPS + GLONASS** *(padrão de fábrica)* | **5 Hz** | 2,5 m · 2,0 m c/ SBAS | 26 s | −167 dBm | `$GNRMC` |
| **GPS apenas** | **10 Hz** | 2,5 m · 2,0 m c/ SBAS | 29 s | −166 dBm | `$GPRMC` |
| GLONASS apenas | 10 Hz | 4 m | 30 s | −166 dBm | `$GLRMC` |
| BeiDou | 10 Hz | 3 m | 34 s | −160 dBm | — |
| Galileo | 10 Hz | 3 m | 45 s | −159 dBm | — |

O módulo sai de fábrica em **recepção concorrente de GPS + GLONASS**, modo em que o teto
é **exatamente 5 Hz**. O requisito não tem **nenhuma folga**: qualquer degradação —
temperatura, sinal fraco, muitos satélites rastreados — e a taxa efetiva cai abaixo do
que o RF01 exige, sem aviso.

**O dado mais útil da tabela:** GPS-apenas tem **precisão horizontal idêntica** à
concorrente (2,5 m, 2,0 m com SBAS). O que se perde são 3 s de cold start, 1 dBm de
sensibilidade e a disponibilidade extra de satélites — que importa em cânion urbano,
justamente onde os radares estão.

**Vale reexaminar o próprio 5 Hz.** A 100 km/h (27,8 m/s), o deslocamento por amostra é:

| Taxa | Deslocamento/amostra a 100 km/h |
| :---: | :--- |
| 5 Hz | 5,6 m |
| 4 Hz | 6,9 m |
| 2 Hz | 13,9 m |

Contra um raio de alerta de **300 m**, mesmo 2 Hz daria granularidade de 14 m — 4,6% do
raio. O 5 Hz é generoso, não apertado.

**Três configurações viáveis:**

| Opção | Configuração | Folga | Custo |
| :---: | :--- | :--- | :--- |
| **a** | GPS+GLONASS @ 5 Hz | **nenhuma** | — |
| **b** | GPS apenas @ 5 Hz | 2× | +3 s cold start, −1 dBm, menos satélites em cânion urbano |
| **c** ⭐ | GPS+GLONASS @ 4 Hz | 20% | 6,9 m/amostra em vez de 5,6 m |

**Recomendação: opção (c).** Mantém a disponibilidade multiconstelação, que é o ativo
real em cidade, e compra 20% de folga trocando 1,3 m de granularidade que o raio de
300 m absorve sem notar. Exige relaxar o RF01 de 5 Hz para 4 Hz.

> **Fecha o R-08 por outro caminho:** a escolha de modo determina o talker NMEA —
> `$GNRMC` em concorrente, `$GPRMC` em GPS-apenas. Aceitar os dois, como o R-08 já
> exigia, deixa o firmware indiferente a esta decisão e permite mudá-la sem tocar no
> parser.

---

## R-28 — Conector permutável com 12 V destrói o aparelho

- **Onde:** `bom_schematic.md` seção 0; BOM itens 18, 19 e 22
- **Confiança:** ✅ Verificado por análise do circuito
- **Origem:** revisão do autor em 2026-09-17
- **Resolvido em:** mesma data, com o perigo reposicionado

**Problema.** Um plugue de **12 V** que encaixe na entrada do aparelho injeta 12 V no nó
`VSYS`, cujo máximo absoluto é **5,5 V**. Pico, GPS, display e cartão saem juntos. O
erro é fácil: dois plugues iguais, sob o painel, no escuro.

### O perigo mudou de lugar duas vezes

| Arranjo | Onde estava o risco |
| :--- | :--- |
| Conversor **dentro** do gabinete | Nos **dois conectores do aparelho**: a entrada trazia 12 V e o buzzer trazia `VSYS`. Trocar destruía tudo. |
| Conversor **fora** *(decisão final)* | Nos **dois cabos externos**: 12 V (piggyback → conversor) e 5 V (conversor → gabinete). |

Com o conversor fora, a entrada do aparelho passou a ser 5 V, então **trocar os dois
conectores do aparelho deixou de ser destrutivo** — o pior caso é o buzzer tocar sozinho
ou o aparelho não ligar.

Mas o risco não desapareceu: migrou para o cabo de 12 V, que agora existe do lado de
fora e pode encaixar na entrada do gabinete.

### Correções aplicadas

1. **O trecho de 12 V não usa JST-XH.** Preferência: ligar direto aos terminais do
   conversor, sem conector — não existindo plugue de 12 V, não há o que trocar. Se
   houver conector, família diferente (VH, faston).
2. **Entrada do aparelho com 3 vias**, pino central sem uso, contra as 2 do buzzer.
   Agora é medida de **robustez**, não de segurança: evita duas formas de perder tempo
   depurando, ao mesmo custo.

Achado do autor, não meu: eu especifiquei os dois conectores sem notar que se tornaram
permutáveis quando a entrada deixou de ser 5 V, e teria mantido o erro se ele não
tivesse pedido a revisão.

---

# 🟠 Pontos relevantes

## R-07 — RF01 (5 Hz) exige configuração UBX não especificada

- **Onde:** `requirements.md` RF01
- **Confiança:** ✅ Verificado (comportamento padrão de fábrica u-blox M8)

**Problema.** O NEO-M8N sai de fábrica em **1 Hz e 9600 bps**. A 9600 bps o conjunto NMEA
padrão a 5 Hz não cabe na banda → frames truncados. O requisito pede a taxa mas não
especifica como obtê-la.

**Correção.** Acrescentar ao RF01 a sequência de configuração obrigatória:
- `CFG-RATE` → 200 ms.
- `CFG-PRT` → 38400 ou 115200 bps.
- `CFG-MSG` → desabilitar todas as sentenças exceto RMC.
- `CFG-CFG` → persistir em BBR/flash **e** reenviar a cada boot (o módulo perde a
  configuração se a bateria de backup estiver descarregada).

---

## R-08 — `$GNRMC` vs `$GPRMC`

- **Onde:** `requirements.md` RF01
- **Confiança:** ✅ Verificado (padrão NMEA: talker ID)

**Problema.** RF01 fixa `$GNRMC`. O talker `GN` só aparece com multi-GNSS ativo; em modo
GPS-only a sentença vira `$GPRMC` e o parser não casa nada — falha silenciosa total.

**Correção.** Aceitar ambos os talkers. Acrescentar também requisito de **validação de
checksum NMEA**, hoje ausente dos dois documentos.

---

## R-09 — As zonas de alerta não saem depois de passar pelo radar

- **Onde:** `requirements.md` RF03; §5 (fluxo de telas); matriz IHM
- **Confiança:** ✅ Verificado (análise lógica do requisito)

**Problema.** RF03 define as zonas apenas por `distância ≤ 300 m`. Ao passar pelo radar
a distância volta a crescer, mas permanece ≤ 300 m por vários segundos: o alerta continua
ativo e, na Zona de Perigo, o buzzer "metralhadora" segue tocando **depois** de já ter
passado o radar. Dois defeitos:

1. **Falta condição de aproximação.** Comparar o azimute veículo→radar com o rumo do
   veículo (alvo à frente), ou exigir distância decrescente entre amostras.
2. **Falta histerese no limiar de 300 m.** Sem ela, os "2 bipes curtos apenas no momento
   da entrada da zona" re-disparam em loop quando a distância oscila na fronteira.

**Consideração adicional.** Raio fixo de 300 m dá ~10,8 s de antecedência a 100 km/h e
~21,6 s a 50 km/h. Avaliar raio proporcional à velocidade.

---

## R-10 — OTA sem integridade, escrita atômica ou rollback

- **Onde:** `requirements.md` RF05; matriz IHM (linha "Fim do download / Falha no Timeout")
- **Confiança:** ✅ Verificado (análise do requisito)

**Problema.** RF05 salva o arquivo baixado "diretamente no cartão SD". Queda de energia
(motorista desliga o carro), timeout ou arquivo remoto vazio deixam a base corrompida e
o dispositivo sem alertas — falha perigosa e silenciosa, já que a tela volta ao
velocímetro normalmente.

**Correção.** Especificar no RF05:
- Baixar para `speedcam.tmp`.
- Validar: tamanho mínimo, contagem de linhas parseáveis, hash conferido contra a origem.
- Só então renomear sobre o original, preservando `speedcam.bak` da versão anterior.
- Rollback automático para `.bak` se a validação falhar.
- Definir **timeout**, **número de tentativas** e comportamento explícito para
  arquivo remoto vazio ou HTTP != 200.

---

## R-11 — Credenciais de Wi-Fi não especificadas e não devem ir no firmware

- **Onde:** `requirements.md` RF05 ("conectar ao ponto de acesso configurado" — sem definir onde)
- **Confiança:** ✅ Verificado (política de segurança da informação aplicável)

**Problema.** O requisito pressupõe um AP "configurado" mas não define o mecanismo.
Credenciais embutidas no firmware violam a política de não manter segredos em código.

**Correção.** Escrever requisito explícito:
- Credenciais em arquivo de configuração no SD (ex. `wifi.cfg`), lido em runtime.
- Tratar o cartão SD como mídia que circula fora do laboratório — não versionar o
  arquivo de credenciais, não usar rede corporativa para o AP de atualização.
- Download por HTTPS ou com verificação de assinatura na origem.

---

## R-12 — Suspender o GPS durante o OTA é risco de uso em movimento

- **Onde:** `requirements.md` RF05
- **Confiança:** ✅ Verificado (análise do requisito)

**Problema.** RF05 "suspende temporariamente a leitura do GPS" ao detectar o clique.
O eixo do encoder é fácil de pressionar sem intenção. Se isso ocorrer em movimento, o
motorista fica sem alerta por tempo indeterminado, e a matriz IHM mostra apenas
"Atualizando base de dados..." — sem indicação de que a proteção está desativada.

**Correção.** Condicionar a entrada em OTA a veículo parado (velocidade < 3 km/h por
N segundos consecutivos), ou exigir confirmação em duas etapas. Definir também timeout
máximo global do modo OTA com retorno automático ao velocímetro.

---

## R-13 — Orçamento de corrente do trilho `3V3_OUT` não foi calculado

- **Onde:** `bom_schematic.md` seções 2, 3 e 4
- **Confiança:** ⚠️ Valores típicos — **requer medição**

**Problema.** Somam-se no `3V3_OUT` (pino 36) o GPS, o display com backlight, os picos
de escrita do SD e o encoder. O total se aproxima do que o regulador interno do Pico
entrega com folga, e o documento não apresentava o cálculo.

O consumo do GPS deixou de ser estimativa — **datasheet UBX-15031086, Tabela 11**:

| Parâmetro | Valor | Nota |
| :--- | ---: | :--- |
| `ICCP` — corrente máxima de pico | **67 mA** | *"use esta figura para dimensionar a capacidade máxima da fonte"* |
| `ICC` tracking contínuo (GPS+GLONASS) | 30 mA @ 3,0 V | regime normal de condução |
| `ICC` aquisição (até o primeiro fix) | 32 mA @ 3,0 V | |
| `ICC_RF` — antena ativa (LNA) | **até 50 mA** | corrente **separada**, sai do pino VCC_RF |

Pior caso do bloco GPS: **67 mA + antena**. Com LNA de antena patch típica (~10–15 mA),
o número de projeto é **~82 mA**; com antena que consuma o máximo de `ICC_RF`, chega a
117 mA. A estimativa anterior de 50–100 mA estava aproximadamente certa, mas agora está
ancorada e o **típico é bem menor** (~40 mA) do que o de projeto.

**Correção.**
- Medir o consumo agregado com todos os periféricos ativos e backlight em 100%.
- Se não houver margem, prever regulador 3V3 dedicado a partir dos 5V.
- Registrar o resultado como requisito não-funcional (ver L-02).

---

## R-14 — Alimentação do GPS: depende do regulador da placa

- **Onde:** `bom_schematic.md` seção 3
- **Confiança:** ✅ **Faixa do chip verificada no datasheet oficial** (UBX-15031086, Tabela 10: min 2,7 · típico 3,0 · máx 3,6 V; máximo absoluto 3,6 V) / ❓ regulador da placa adquirida a identificar
- **Atualizado em:** 2026-09-15

**Problema.** A revisão 1 oferecia "5 V **ou** 3V3_OUT" sem decidir — são circuitos
diferentes. Eu havia adotado 3,3 V como suposição conservadora. **A suposição estava
mal fundamentada.**

O datasheet do CI u-blox especifica 2,7 V a 3,6 V. Esse é o limite **do chip**, não da
placa breakout — e essa distinção decide o circuito, porque a maioria das breakouts traz
LDO embarcado com faixa de entrada própria.

| Regulador na placa | Alimentar em | Motivo |
| :--- | :--- | :--- |
| Nenhum | **3,3 V** obrigatório | 5 V excede o máximo absoluto de 3,6 V e destrói o chip |
| LDO de baixa queda (HT7333, XC6206) | **5 V** preferível | 3,3 V funciona (~3,1 V), mas 5 V alivia o regulador do Pico (R-13) |
| **LDO de queda alta (AMS1117-3.3)** | **5 V** obrigatório | Com 3,3 V de entrada a saída cai a ~2,1 V — **abaixo do mínimo de 2,7 V** |

O terceiro caso é o traiçoeiro: alimentar em 3,3 V uma placa com AMS1117 deixa o CI
subalimentado. O sintoma é ausência de fix ou fix intermitente, que se confunde
facilmente com problema de antena ou de céu obstruído — e não com erro de projeto
elétrico.

**Em duas das três configurações os 5 V são preferíveis ou obrigatórios**, o que
inverte a suposição que eu havia aplicado.

**Correção.** Identificar o regulador antes de fechar o circuito: inspeção visual do
encapsulamento de 3 pinos junto ao VCC, leitura da marcação, ou medição da tensão no CI
com 5 V na entrada. Procedimento detalhado em `bom_schematic.md` §3.

**Efeito colateral positivo — questão de nível lógico encerrada.** Em qualquer das três
configurações válidas o CI opera em 3,3 V ou menos, logo o TX sai em nível compatível
com o GPIO 1. Não há necessidade de divisor nem level shifter, e a ressalva que eu havia
levantado sobre isso não se aplica.

---

## R-15 — Concorrência no SPI0 e divisão entre cores não especificadas

- **Onde:** `requirements.md` RNF04; `bom_schematic.md` seção 2
- **Confiança:** ✅ Verificado (requisito de inicialização SD / arquitetura RP2350)

**Problema.** O barramento compartilhado com dois CS distintos está correto, mas:
- O SD exige clock ≤ 400 kHz **na inicialização**, enquanto o ST7789 opera em dezenas
  de MHz — troca de velocidade por dispositivo não está prevista.
- Nenhum acesso pode se intercalar com o outro; não há requisito de exclusão mútua.
- O RNF04 fala de IRQs e timers mas não define a divisão de trabalho, apesar de o
  Pico 2 W ser dual-core.

**Correção.** Especificar no RNF04: core 0 = GPS/parsing/cálculo geográfico;
core 1 = display/encoder/LEDs; mutex no acesso ao SPI0; reconfiguração de clock por
dispositivo antes de cada transação.

---

## R-16 — O diodo 1N4148 no buzzer é desnecessário e a justificativa está incorreta

- **Onde:** `bom_schematic.md` item 13 e seção 5; `requirements.md` RNF02
- **Confiança:** ✅ Verificado (buzzer piezoelétrico é carga capacitiva)

**Problema.** O item 13 justifica o diodo como "roda livre para absorver o pico de
retorno magnético do buzzer". O SFM-27 é piezoelétrico **ativo** — carga capacitiva com
oscilador interno, sem indutância significativa. Não há pico de retorno magnético a
absorver. A justificativa técnica no documento está errada.

**Correção.** Corrigir o texto. O componente pode ser mantido (é inofensivo e protege
contra troca futura por buzzer eletromagnético), mas não deve ser descrito como proteção
necessária. A **orientação descrita está correta** para flyback (catodo no 5V, anodo no
coletor) — preservar caso o diodo seja mantido.

---

## R-17 — BC547 opera no limite de corrente

- **Onde:** `bom_schematic.md` item 10, seção 5; `requirements.md` RNF02
- **Confiança:** ⚠️ Valor típico (consumo do SFM-27 — **medir**)

**Problema.** O BC547 tem Ic máximo de 100 mA. Um piezo de 95–105 dB pode puxar
30–50 mA, somando a capacitância do cabo longo até o painel. Funciona, mas sem margem.

**Correção.** Trocar por BC337 (Ic 500 mA) ou 2N2222 (Ic 800 mA) — mesmo encapsulamento
TO-92, mesmo custo, mesma pinagem a confirmar por datasheet. Com 1 kΩ na base a corrente
é ~2,6 mA, suficiente para saturar qualquer um dos três. Medir o consumo real do buzzer
adquirido.

---

## ~~R-18 — Mapeamento dos campos do iGO8~~ → ✅ **RESOLVIDO**

- **Onde:** `requirements.md` RF02 / RF05
- **Solução registrada em:** `formato_dados.md` §0 e §5; conversor em `converte.py`
- **Resolvido em:** 2026-09-15

O arquivo real (18.294 pontos) traz **cabeçalho explícito**:

```
X,Y,TYPE,SPEED,DirType,Direction
```

Minha previsão de que a ordem seria `X,Y` com longitude primeiro **estava certa** — e
essa era a armadilha de falha silenciosa. Confirmado.

Minha previsão de que `Direction = 0` seria o sentinela de omnidirecional **estava
errada.** O `Direction` vem preenchido mesmo quando `DirType = 0` (99,6% não-zero); o
sentinela é o próprio campo `DirType`. Semântica confirmada por teste geométrico —
proporção de registros com "gêmeo" de rumo oposto a menos de 120 m:

| `DirType` | n | Com gêmeo oposto | Conclusão |
| :---: | ---: | ---: | :--- |
| 0 | 1.419 | 0,3% | omnidirecional |
| 1 | 15.218 | **47,9%** | unidirecional (pista oposta = registro separado) |
| 2 | 1.657 | 0,2% | bidirecional |

Contraste de ~200×, conclusivo.

**Decisão revisada:** eu havia recomendado expandir cada bidirecional em dois registros.
Com os números reais isso custaria 19,4 KB de RAM a mais (233,8 KB contra 214,4 KB), e a
comparação bidirecional é uma dobra aritmética trivial. **Guardar o `DirType` em 2 bits
venceu a expansão** — ver `formato_dados.md` §3.2.

Conversor escrito, executado e verificado: 18.294 registros, zero rejeições, round-trip
de coordenadas idêntico ao `.txt`, CRC-32 e invariante de ordenação conferidos.

---

## R-19 — `limite = 0` dispara Zona de Perigo permanente → 🟨 **comportamento decidido, implementação pendente**

- **Onde:** `requirements.md` RF03 (Zona de Perigo) + matriz IHM (falta a linha)
- **Confiança:** ✅ Verificado nos dados reais
- **Decisão do autor:** 2026-09-15
- **Detalhe em:** `formato_dados.md` §7.1

**Problema.** **1.430 registros — 7,8% da base — têm `SPEED = 0`**: são os semáforos
(`TYPE=3`), que não têm limite de velocidade associado. O RF03 define a Zona de Perigo
como `Velocidade Carro > Limite Radar`; com `Limite = 0`, **qualquer** velocidade acima
de zero satisfaz a condição. Todo semáforo dispararia fundo vermelho piscante e buzzer
em "metralhadora" até o carro parar — inutilizável justamente na cidade.

Bug que só aparece com a base real na mão: a lógica do RF03 é perfeitamente coerente
até encontrar um ponto sem limite aferível.

**Decisão: nova Zona de Semáforo, silenciosa.**

| Aspecto | Comportamento |
| :--- | :--- |
| Buzzer | 🔇 **silencioso — sem exceção** |
| Visor | ícone de semáforo + distância decrescente, **sem placa de limite** |
| LED RGB | 🟡🔴 **amarelo/vermelho alternados a 2 Hz** |
| Zona de Perigo | **nunca**, independente da velocidade |

**Pendente de implementação:**
- [ ] Nova linha na matriz IHM do `requirements.md` (texto pronto em `formato_dados.md` §7.1)
- [ ] Gatilho por `limite == 0`, **não** por `TYPE == 3` — os 22 registros combinados
      (semáforo *com* limite 50/60) devem seguir pelo caminho de radar de velocidade
- [ ] Alternância em timer de hardware, conforme RNF04
- [ ] Regra de prioridade quando semáforo e radar coexistem nos 300 m (proposta na §7.1)

> ⚠️ **Esta decisão tem pré-requisito de hardware: o R-05.** Ver nota no R-05.

---

## R-20 — `TYPE=5` → ✅ **RESOLVIDO: rótulos oficiais obtidos, minhas duas inferências estavam erradas**

- **Onde:** `requirements.md` RF03.5, RF03.10; `formato_dados.md` §0.2, §7.2
- **Confiança:** ✅ **Aritmeticamente conclusivo** — contagens batem na unidade
- **Resolvido em:** 2026-09-16

### Como foi resolvido

A fonte da base permite contar pontos por categoria de equipamento. Comparando com a distribuição de `TYPE` no arquivo
baixado, o mapeamento sai sem ambiguidade:

| Categoria de equipamento | n | → `TYPE` | n no arquivo |
| :--- | ---: | :---: | ---: |
| Radar Fixo | 11.783 | **1** | 11.783 |
| **Radar Móvel** | 2.065 | **5** | 2.065 |
| **Semáforo c/ Câmera** | 1.452 | **3** | 1.452 |
| **Semáforo c/ Radar** | 2.994 | **2** | 2.994 |

E `1,2,4,5` é a única combinação de categorias que soma 18.294 — confirmando também
qual seleção gerou o arquivo atual.

### Minhas duas inferências estavam erradas

| `TYPE` | Meu rótulo inferido | Rótulo oficial |
| :---: | :--- | :--- |
| 2 | "provável lombada eletrônica" | **Semáforo c/ Radar** |
| 5 | "provável trecho controlado (velocidade média)" | **Radar Móvel** |

Eu as derivei do perfil de velocidade e da distribuição espacial. Ambas eram
plausíveis e ambas estavam erradas. O caso do `TYPE=5` é instrutivo: passei por duas
análises espaciais — a primeira com erro de método, a segunda corrigida e apontando
"evidência mista" — e **nenhuma das duas chegou perto** do rótulo real. Geometria
sozinha não recupera semântica; a fonte de dados tinha a resposta.

### O mapeamento explica as anomalias

* `TYPE=3` tem `SPEED=0` em 98,5% porque é **câmera de avanço de sinal** e não afere
  velocidade. Os 22 casos com 50/60 são inconsistências da base, não equipamento
  combinado como eu supus.
* `TYPE=2` sempre tem limite porque afere **as duas coisas** — daí a faixa urbana.
* `TYPE=5` opera em 60–120 km/h porque **fiscalização móvel** acontece em rodovia. Os
  pares a ~26 m em sentidos opostos são pontos de operação registrados por sentido, a
  mesma convenção dos radares fixos.

### Consequências aplicadas

| Item | Resultado |
| :--- | :--- |
| **RF03.5** | Reescrito de "Trecho controlado" para **Radar Móvel**. Zonamento inalterado. |
| **RF03.10** | ❌ **ANULADO.** Sem trecho controlado na base, não há par de pórticos a cronometrar. O número fica retirado, não reservado. |
| **RF03.3** | Lacuna nova — ver R-26. |

O que a correção **não** invalida: o zonamento de `TYPE=5` como "radar de velocidade
simples" sempre esteve correto. Radar Móvel afere velocidade instantânea.

---

## R-22 — GPIO do Pico em 3,3 V excede a faixa de entrada do GPS

- **Onde:** `bom_schematic.md` seção 3 (GPS RX ← Pico GPIO 0)
- **Confiança:** ✅ **Verificado no datasheet oficial** (UBX-15031086, Tabelas 9 e 10)
- **Registrado em:** 2026-09-15

**Problema.** O datasheet especifica para os pinos digitais de entrada:

| Parâmetro | Valor | Fonte |
| :--- | :--- | :--- |
| Faixa de operação `VIN` | **0 a VCC** | Tabela 10 (operating conditions) |
| Máximo absoluto `VIN` | VCC + 0,5 V *(se VCC < 3,1 V)* / 3,6 V *(se VCC > 3,1 V)* | Tabela 9 |
| `VCC` NEO-M8N | min 2,7 · **típico 3,0** · máx 3,6 V | Tabela 10 |
| Corrente por pino `IPIN` | máx 10 mA | Tabela 9 |

O Pico aciona o GPIO 0 em **3,3 V**. Como o VCC **típico** do NEO-M8N é **3,0 V**, a
faixa de operação da entrada fica em 0–3,0 V e os 3,3 V do Pico a **excedem em 0,3 V**.
Não há dano — o máximo absoluto nesse caso é VCC + 0,5 = 3,5 V — mas é operação fora de
especificação, e a margem até o limite absoluto é de apenas 0,2 V.

Se a placa tiver LDO de 3,3 V, `VIN` máximo passa a 3,3 V e a situação é limítrofe mas
dentro da faixa. Se o LDO for de 3,0 V, ou se o VCC cair sob carga, fica fora.

**Esta direção do barramento é necessária:** os comandos UBX de configuração do RF01.2
vão do Pico para o GPS.

**Correção.** **Resistor de 1 kΩ em série** no caminho `Pico GPIO 0 → GPS RX`. Limita
qualquer corrente de clamp a menos de 1 mA (contra o `IPIN` de 10 mA) e é eletricamente
irrelevante a 115200 bps: com ~10 pF de capacitância de pino, a constante RC é de ~10 ns
contra um bit de 8,7 µs.

**A direção oposta está confirmada como segura**, agora com números:

| | Valor | Conclusão |
| :--- | :--- | :--- |
| `VOH` do GPS (Tabela 10) | ≥ VCC − 0,4 V a IOH = 4 mA | 2,9 V com VCC 3,3 · 2,6 V com VCC 3,0 |
| `VIH` do RP2350 | ≈ 0,65 × 3,3 = 2,15 V | Ambos os casos leem nível alto com margem |

Nenhum divisor ou level shifter é necessário no `GPS TX → Pico GPIO 1`.

---

## R-23 — Faixas de 10%/20% ficam vazias em 82,5% dos radares da base

- **Onde:** `requirements.md` RF03.7 (escalonamento sonoro da Zona de Perigo)
- **Confiança:** ✅ **Verificado na base real** (base real, 16.864 radares de velocidade)
- **Origem:** especificação de comportamento do autor, 2026-09-15
- **Resolvido em:** mesma data, ancoragem reformulada

**Problema.** O autor especificou duas regras que, combinadas literalmente, se anulam:

1. **Tolerância legal absoluta** de 7 km/h até 100 km/h (RF03.6).
2. **Faixas sonoras** a 10% e 20% "acima da velocidade da via" (RF03.7).

Sete km/h é um percentual **grande** em via lenta — a 30 km/h equivale a 23%, já acima
da própria faixa de 20%. Ancorando os percentuais no limite da via, a faixa de bipes
lentos nasce **abaixo** do limiar em que a Zona de Perigo começa:

| Limite | `V_infra` | `limite × 1,10` | Faixa 1 |
| :--- | ---: | ---: | :--- |
| 30 | 37,0 | 33,0 | ❌ vazia — *e a faixa 2 (36,0) também* |
| 40 | 47,0 | 44,0 | ❌ vazia |
| 50 | 57,0 | 55,0 | ❌ vazia |
| 60 | 67,0 | 66,0 | ❌ vazia |
| 70 | 77,0 | 77,0 | ❌ vazia |
| 80 | 87,0 | 88,0 | +1,0 km/h |
| 120 | 126,0 | 132,0 | +6,0 km/h |

**Alcance medido na base:** os limites ≤ 70 km/h são **13.905 de 16.864 radares de
velocidade — 82,5%**. Em todos eles a faixa de bipes lentos seria vazia. E a 30 km/h
até a intermediária seria vazia, fazendo o aparelho saltar direto para **bipe contínuo**
no instante em que cruzasse o limiar de infração — o oposto exato do escalonamento
pretendido, e precisamente nas vias urbanas onde o aparelho mais será usado.

É o tipo de defeito que não aparece na leitura da especificação: as duas regras são
individualmente corretas e a interação entre elas é que falha.

**Correção aplicada.** Os percentuais passam a contar a partir de **`V_infra`** (o
limiar de infração) e não do limite da via. Preserva a intenção do escalonamento em
passos de 10% e garante faixas não vazias e monotônicas em todos os dez limites da base.
Tabela completa em `requirements.md` RF03.7.

**Ganho colateral:** as fronteiras da nova ancoragem ficam próximas dos degraus de
gravidade da legislação em vias rápidas, onde a tolerância percentual e a absoluta
convergem.

---

## R-24 — Precedência por categoria suprimiria o aviso de margem

- **Onde:** `requirements.md` RF03.4 vs RF03.9
- **Confiança:** ✅ Verificado na base real (29,2% de coexistência)
- **Origem:** interação entre duas decisões do autor, 2026-09-15
- **Resolvido em:** mesma data

**Problema.** O autor escolheu a opção (d) para o RF03.4 — precedência por categoria,
com Semáforo acima de Aproximação — e, na mesma decisão, aprovou o sub-estado de
**margem** (rosa piscante entre o limite e `V_infra`).

As duas combinadas criam um furo: um semáforo em **qualquer** ponto da janela de 300 m
suprimiria o rosa piscante, e o motorista a 1 km/h do limiar de multa não receberia
aviso. Como **29,2% dos semáforos da base têm radar de velocidade a menos de 300 m**, o
aviso de margem seria perdido justamente nas vias urbanas onde ele mais importa.

A justificativa original da opção (d) era que *"a Aproximação é o único estado sem ação
associada"* — argumento que o RF03.9 tornou verdadeiro **apenas para o sub-estado
conforme**. A margem tem ação clara (não acelerar) e certeza (o aparelho comparou a
velocidade com `V_infra`).

**Correção aplicada.** A precedência passa a ter quatro níveis, com a margem elevada
acima do semáforo:

```
Perigo  >  Margem  >  Semáforo  >  Conforme
```

Dentro de cada nível, vence o mais próximo. Exige rastrear **quatro candidatos** na
varredura — um por categoria — em vez de um único alvo.

**Achado colateral.** Ao especificar os quatro ponteiros, ficou explícito um erro sutil
que a implementação ingênua cometeria: **o estado é por ponto, não global.** Dois radares
na janela com limites diferentes — um de 60 km/h a 250 m e um de 80 km/h a 100 m, veículo
a 70 km/h — produzem simultaneamente um candidato de Perigo (o mais distante) e um de
conforme (o mais próximo). Avaliar o estado do radar *mais próximo* e usá-lo como estado
do sistema perderia a infração em curso.

---

## R-25 — Paleta do LED RGB saturada em nove estados → ✅ **RESOLVIDO**

- **Onde:** `requirements.md` RF03.9 e matriz IHM
- **Decisão do autor:** 2026-09-15

**Problema.** Com a adição do rosa (RF03.9), o LED RGB acumulava **nove** estados
distintos — segura, conforme, margem, perigo, semáforo, boot, sem sinal, OTA e falha de
dados — distinguidos por hue e taxa de piscada. Perto do limite do que um LED difuso
comunica com confiabilidade na visão periférica, e misturando duas categorias
semânticas: estado da **via** e estado do **aparelho**.

**Decisão.** Os estados de **boot, sem sinal, OTA e falha de dados** passam a ser
exibidos **somente na tela**. O LED RGB fica apagado neles, reduzindo a paleta a
**cinco** estados, todos da mesma categoria — "qual é a sua situação em relação à via".

| Estado | LED RGB |
| :--- | :--- |
| Zona Segura | 🟢 verde fixo |
| Aproximação conforme | 🟡 amarelo fixo |
| Aproximação em margem | 🩷 rosa piscante (1 Hz) |
| Zona de Perigo | 🔴 vermelho piscante (4 Hz) |
| Zona de Semáforo | 🟡🔴 amarelo/vermelho alternados (2 Hz) |
| *(todos os demais)* | **apagado** |

**Consequência não óbvia e favorável:** o **LED apagado ganha significado próprio —
nenhuma proteção ativa.** É inequivocamente distinguível do verde fixo, então um olhar
de relance informa que o sistema não está avaliando a via. Um LED colorido nesses
estados diria "estou trabalhando" sem estar.

O canal **azul** deixa de ser usado isoladamente e serve apenas à mistura do rosa (R+B).

**Pendência que este achado gerou, já resolvida:** o LED verde dedicado ao status de
Wi-Fi ficou redundante e **saiu do projeto** em 2026-09-15, liberando o **GPIO 14** —
que passou a ser usado pelo card detect do leitor SD (2026-09-17).

---

## R-26 — `TYPE=2` é semáforo e não recebe aviso de semáforo

- **Onde:** `requirements.md` RF03.3
- **Confiança:** ✅ Verificado no mapeamento oficial (R-20)
- **Registrado em:** 2026-09-16

**Problema.** Os **2.994 pontos de `TYPE=2` são Semáforos c/ Radar** — fiscalizam
avanço de sinal **e** velocidade. O gatilho da Zona de Semáforo (RF03.3) é
`limite == 0`, e esses pontos têm limite. Resultado: caem no caminho de radar de
velocidade e o motorista **não recebe indicação alguma de fiscalização de sinal** em
2.994 cruzamentos urbanos.

A regra `limite == 0` continua correta para o *zonamento* — há velocidade a comparar,
então a máquina de estados deve tratá-los como radar. O que falta é **indicação
visual** de que ali também se fiscaliza o sinal.

**Correção.** O campo `TYPE` já está preservado nos flags do registro
(`formato_dados.md` §3.1), então não há mudança de formato: zonamento pela velocidade,
ícone combinado de semáforo + radar na tela.

> 🎨 **Forma visual deferida** para a sessão dedicada a telas e interfaces, junto com
> a distinção visual de Radar Móvel (RF03.5) e o sinal de alerta de tipo de ponto.

---


## R-29 — O `.fzz` versionado divergiu do gerador

- **Onde:** `coruja_gps.fzz` vs `gera_fritzing.py`
- **Registrado em:** 2026-09-17

**Situação.** O `.fzz` foi gerado por script e depois **ajustado à mão** no Fritzing:
roteamento com pontos de dobra (53 → 155 fios), orientação de 16 peças e 4 notas
rotulando os módulos que aparecem como headers genéricos. Nada disso o gerador
reproduz.

Regerar o arquivo **descarta o trabalho manual**. E o gerador já foi atualizado para a
entrada de 12 V com 3 vias, enquanto o `.fzz` ainda mostra a de 5 V com 2 — a
divergência existe agora.

**Convenção adotada:** o gerador é fonte da **netlist**; o `.fzz` é fonte do
**layout**. Para mudar fiação: editar `NETS`, regerar num arquivo temporário, comparar
as redes e aplicar a mudança à mão no Fritzing.

O documento de referência para montagem é o **`bom_schematic.md`**, não o `.fzz`.

### Como fechar a divergência à mão, no Fritzing

O único ponto divergente é o conector de entrada: o `.fzz` tem 2 vias rotuladas
`+5V / GND`, o gerador tem 3 vias `+12V / n/c / GND`. São quatro passos e dois fios.

1. **Na vista Protoboard, apague o conector de entrada atual.** É o header de 2 vias
   com título *"Entrada 5V (carregador veicular)"*, ligado ao anodo do `D1`
   (Schottky) e ao GND. Apagar leva os dois fios com ele.

2. **Arraste um header fêmea de 3 vias** para a mesma posição. Na paleta:
   *Core Parts → Connection → Generic female header*, e ajuste `pins` para **3**.
   Se preferir garantir a peça exata, é a `generic-female-header-rounded_3`,
   moduleId `b13c0353-1ee1-11de-8283-0019d2b7521e`.

3. **Renomeie** para `Entrada 12V (pos-chave) -> conversor CC`, em *Inspector →
   part label*. Se quiser, acrescente uma nota ao lado, como você fez com os módulos.

4. **Refaça os dois fios**, deixando o pino central sem ligação:

   | Do pino | Para | Cor |
   | :--- | :--- | :--- |
   | 1 — `+12V` | anodo do `D1` (1N5817) | vermelho `#ff1a1a` |
   | 2 — `n/c` | *nada* | — |
   | 3 — `GND` | rede de GND | preto `#000000` |

> ⚠️ **Fisicamente, o `D1` fica depois do conversor CC**, não antes — o conversor não
> está representado no esquemático, que começa no 5 V. Ligar `+12V` ao `D1` no desenho
> é atalho de representação. Quem monta segue a **seção 0** do `bom_schematic.md`, não
> o `.fzz`.

Ao terminar, confira que o total de peças voltou a 21 e que nenhuma rede ficou órfã:
o `gera_fritzing.py` continua sendo a referência das 30 redes.

## R-30 — A falha de dados avisa por 3 s e depois cala para sempre

- **Onde:** `requirements.md` matriz de IHM, linha "Cartão SD ausente ou base inválida"
- **Confiança:** ✅ Verificado no próprio texto da matriz
- **Registrado em:** 2026-09-17
- **Status:** ✅ **CORRIGIDO** na mesma data — `requirements.md` §4.1

**Problema.** A matriz mandava exibir *"Base indisponível — modo velocímetro"* e
**permanecer na tela 3 s**, voltando depois ao velocímetro. Passados os 3 segundos, a
tela ficava idêntica à Zona Segura — número branco, LED apagado — e o motorista rodava o
resto da viagem **sem nenhuma proteção e sem nenhum indício disso**.

O LED não cobre o furo: em Falha de Dados ele fica **apagado** por decisão do R-25, e
apagado é indistinguível de aparelho desligado na visão periférica.

A ambiguidade é de significado, não de pixel: a tela de Zona Segura afirma *"não há
pontos próximos"*. Sem base, o que o aparelho sabe é *"não sei se há pontos"*. As duas
afirmações eram visualmente iguais, e a segunda é perigosa porque produz confiança
infundada — exatamente a classe de falha silenciosa que o RF07 foi escrito para evitar.

**Correção aplicada.** A mensagem passa a ser **persistente**, na faixa inferior, em
cinza. O encaixe é exato e foi o autor quem o apontou: **sem base não existe ponto, logo
a barra de proximidade nunca teria o que mostrar** — a faixa inferior está livre 100% do
tempo nesse estado, e o relógio nem precisa ceder lugar.

A mesma lógica generalizou a faixa inferior para um canal único — *"há alerta, ou por que
não há"* — que também absorveu o estado "sem sinal" e a recusa de OTA em movimento. Ver o
inquilinato em `requirements.md` §4.1.

## R-31 — A entrada de 12 V não tinha proteção nenhuma

- **Onde:** `bom_schematic.md` seção 0
- **Confiança:** ✅ Verificado no próprio desenho
- **Registrado em:** 2026-09-18
- **Status:** ✅ **CORRIGIDO** na mesma data — BOM itens 27 e 28

**Problema.** A cadeia de alimentação tinha três proteções, e nenhuma cobria o
conversor:

| Proteção | Protege | Não protege |
| :--- | :--- | :--- |
| Fusível de 2 A | o **fio** contra curto | sobretensão — fusível não reage a transiente de milissegundos |
| `C1` 470 µF em `VSYS` | o **Pico** contra ruído e queda | o conversor: fica **depois** dele |
| `C2` 100 nF em `VSYS` | idem, alta frequência | idem |

O conversor CC era **o único componente exposto direto à rede do carro**, e o
desenho contava com ele aguentar sozinho. Apareceu quando o autor perguntou se
um módulo específico de 3 A era seguro: a resposta exigia olhar o que havia a
montante dele, e não havia nada.

**Correção aplicada.** TVS bidirecional de 24 V (item 27) e eletrolítico de
470 µF / 50 V (item 28), em paralelo na entrada de 12 V, junto ao conversor.

**Sobre o que isso cobre — e o que não cobre.** Registro aqui porque eu mesmo
exagerei ao propor: disse que o TVS "existe justamente para load dump". Está
errado. Um load dump real chega a 60–120 V com impedância de fonte de ordem de
ohms por até 400 ms; contra um clamp de 33 V isso dá dezenas de ampères e mais
de **1 kW sustentado**, e 600 W ou 1500 W são ratings de pulso de 10/1000 µs,
não de centenas de milissegundos. Nenhum TVS axial pequeno sobrevive.

O que o TVS cobre são os transientes **frequentes e de baixa energia** —
chaveamento indutivo, ruído de ignição, pulsos da ISO 7637-2 — que são os que
matam módulo barato no uso diário. Para o load dump, o projeto se apoia no fato
de que alternador de carro moderno já clampa internamente em ~35 V. **É uma
suposição, não uma medição**, e vale dizer isso em voz alta em vez de deixar o
componente novo dar falsa sensação de blindagem.

**Detalhe que quase passou.** O sufixo `CA` é obrigatório: significa
bidirecional. A versão `A` é unidirecional e, montada ao contrário, fica em
curto permanente com 12 V atrás. E o fusível tem de ficar **antes** da proteção
no percurso do cabo: o modo de falha desejável de um TVS é curto, e sem fusível
a montante ele vira aquecedor ligado na bateria.

---

# 🟡 Lacunas de requisitos

## L-01 — Comportamento sem fix de GPS

Nenhum requisito define o que acontece em túnel, garagem ou cold start: o que a tela
mostra, o que o LED RGB faz, se o último radar detectado continua válido e por quanto
tempo. Hoje a matriz IHM não tem linha para esse estado.

## L-02 — Orçamentos de recursos

Não há requisito não-funcional para **memória**, **corrente** (ver R-13) nem **tempo de
boot** até o primeiro alerta útil — o TTFF do NEO-M8N em cold start pode passar de 30 s,
e o motorista não tem como saber que ainda não há proteção.

O orçamento de memória já tem números: `formato_dados.md` §1 estima **~407 KB de 520 KB**
(base 214 KB + framebuffer 150 KB + pilha Wi-Fi ~48 KB + resto), com duas alavancas de
alívio identificadas. Falta **transformar isso em requisito** e **medir** os itens
marcados ⚠️ com `arm-none-eabi-size` e marca d'água de stack.

Dois números que merecem virar limites explícitos:
- **Framebuffer: 150 KB** — maior consumidor isolado, e não aparecia em nenhum documento.
- **Base: 214 KB hoje, teto de ~35.000 registros** antes de o particionamento do
  Anexo A voltar a ser necessário.

## L-03 — Falha ou ausência do cartão SD

Não especificado: boot sem cartão, cartão não-FAT32 (o RNF03 exige FAT32 mas não define
o comportamento quando a exigência não é atendida), `speedcam.txt` ausente ou ilegível.

## L-04 — Watchdog e recuperação de travamento

Crítico neste produto: o motorista não pode reiniciar o dispositivo dirigindo. Não há
requisito de watchdog, nem de comportamento após reset inesperado.

## L-05 — Faixa térmica e especificação de temperatura dos componentes

Painel de veículo ao sol passa de 60 °C. O capacitor eletrolítico (item 14) não tem
temperatura especificada — **exigir 105 °C, não 85 °C**. Avaliar também a faixa de
operação do display IPS e do cartão SD. Nenhum RNF trata disso.

## L-06 — Conversão e tratamento da velocidade

A sentença RMC entrega velocidade em nós. O fator ×1,852 e a estratégia de suavização
(média móvel, filtro) não estão especificados, apesar de a velocidade ser o gatilho da
transição Aproximação → Perigo.

## L-07 — Estratégia de verificação e testes

Nenhum dos dois documentos menciona verificação. Mínimo necessário:
- Testes unitários do parser NMEA (incluindo checksum inválido, campos vazios, sem fix).
- Testes unitários do Haversine e do filtro de rumo com vetores conhecidos —
  **incluindo o caso de wraparound de R-04**.
- Replay de logs NMEA gravados em estrada, para validar transições de zona sem dirigir.
- Teste de interrupção de energia durante o OTA (valida R-10).

---

## L-08 — Linguagem e runtime do firmware não declarados

Nenhum dos documentos diz se o firmware é **C/C++ (Pico SDK)** ou **MicroPython**. Para
esta arquitetura a escolha não é livre: o orçamento de `formato_dados.md` §1 assume
overhead de runtime de ~20 KB, compatível com o SDK em C. O heap do MicroPython mais o
interpretador não deixariam espaço para 214 KB de base **e** 150 KB de framebuffer — e
nesse caminho o particionamento em quadrantes (Anexo A) voltaria a ser obrigatório.

**Comparação completa de desempenho em `formato_dados.md` §9.** Resumo: **velocidade não
é o critério** — MicroPython usaria ~4% do orçamento de ciclo, e o custo dominante é a
transferência SPI do display (~29 ms), igual nas duas linguagens. Os critérios reais são
**memória** e, sobretudo, **dual core**: o `_thread` do port RP2 tem GIL, então a divisão
core 0 = GPS / core 1 = UI que o RNF04 e o R-15 exigem não se realiza em MicroPython.

**Recomendação:** C/C++ com o Pico SDK, registrado como requisito não-funcional. É a
decisão de maior alavancagem em aberto no projeto — resolve ou reabre R-02. O teste de
heap da §9.5 fecha a dúvida em 30 segundos no hardware real.

## L-09 — Precisão numérica não especificada (FPU de precisão simples)

A FPU do Cortex-M33 do RP2350 é de **precisão simples**; `double` é emulado em software.
Nenhum documento especifica em que precisão rodam os cálculos geográficos, e a combinação
que parece natural é justamente a errada: `float` tem ~7,2 dígitos significativos,
enquanto uma coordenada como `-23.537216` precisa de 8 — e a Haversine subtrai números
próximos, ampliando o erro exatamente em distâncias curtas.

Ver `formato_dados.md` §4.1: a forma **equirretangular sobre diferenças** é 5–8× mais
barata, tem erro <0,01% em 300 m e escapa do problema naturalmente, porque opera sobre
números pequenos. Escrever como requisito, junto com a decisão sobre manter ou não a
Haversine do RF02.

# ⚪ Correções editoriais

| # | Onde | Atual | Correto |
| :---: | :--- | :--- | :--- |
| E-01 | `bom_schematic.md`, tabela BOM, última linha | Item **29** | Item **19** |
| E-02 | `requirements.md` RNF05 | "Moduloridade Mecânica" | "Modularidade Mecânica" |
| E-03 | `requirements.md` RNF02 | "transistor NPN acuado como chave" | "transistor NPN atuando como chave" |
| E-04 | `requirements.md`, matriz IHM, linha Zona Segura | "Fondo Preto" | "Fundo Preto" |
| E-05 | `bom_schematic.md` seção 1 + `requirements.md` RNF01 | Referências a `VBUS` como nó de entrada | Atualizar em conjunto ao aplicar **R-01** |

---

# ⚖️ Nota

Se o dispositivo for de fato instalado no carro, vale saber que no Brasil há distinção
entre **detectores/inibidores de radar** (proibidos) e **bases de dados de posição de
radares fixos** com alerta por GPS — categoria em que este projeto se enquadra, como
navegadores comerciais. Não verifiquei a resolução CONTRAN vigente; irrelevante para
bancada, mas bom saber antes de fixar no painel.

---

# ✅ Checklist de fechamento

```text
BLOQUEADORES
[ ] R-01  Entrada de energia migrada para VSYS (pino 39) + Schottky
[x] R-02  RESOLVIDO — premissa errada; 18.294 pontos cabem em RAM (formato_dados.md §1)
[x] R-03  RESOLVIDO — array ordenado por lat + busca binária (formato_dados.md §4)
[ ] R-04  Diferença circular + porta de velocidade + DirType (0=omni, 2=bi)
[~] R-19  DECIDIDO: Zona de Semáforo silenciosa, LED amarelo/vermelho 2 Hz
                └─ implementação pendente; DEPENDE do R-05 (sem verde, não há amarelo)
[ ] R-05  Resistores do LED RGB recalculados e medidos  <-- pré-requisito do R-19
[ ] R-06  Conector do buzzer trocado ou protegido
[x] R-21  RESOLVIDO — GPS+GLONASS a 4 Hz nominal, piso de 3 Hz
[ ] R-28  Trecho de 12 V sem conector JST-XH (direto no conversor)

RELEVANTES
[ ] R-07  Sequência de configuração UBX especificada no RF01
[ ] R-08  Parser aceita $GNRMC e $GPRMC + valida checksum
[ ] R-09  Condição de aproximação + histerese nas zonas
[ ] R-10  OTA com validação, escrita atômica e rollback
                └─ validação de integridade já especificada em formato_dados.md §2
[ ] R-11  Credenciais de Wi-Fi fora do firmware
[ ] R-12  OTA condicionado a veículo parado
[ ] R-13  Orçamento de corrente do 3V3_OUT medido
[ ] R-14  Alimentação do GPS definida (5V ou 3V3)
[ ] R-15  Exclusão mútua no SPI0 + divisão entre cores no RNF04
[ ] R-16  Justificativa do diodo corrigida
[ ] R-17  Transistor com margem de corrente (BC337 / 2N2222)
[x] R-18  RESOLVIDO — cabeçalho explícito + DirType confirmado; converte.py verificado
[ ] R-22  Resistor de 1 kΩ em série no `GPIO 0 → GPS RX`
[x] R-23  RESOLVIDO — faixas sonoras reancoradas em V_infra
[x] R-24  RESOLVIDO — precedência Perigo > Margem > Semáforo > Conforme
[x] R-25  RESOLVIDO — LED RGB exclusivo de estado de via, 5 estados
[x] R-26  RESOLVIDO — ícone 🚦+🏎 composto nos 2.994 pontos de TYPE=2 (requirements.md §4.1)
[x] R-30  RESOLVIDO — BASE INDISPONÍVEL persistente na faixa inferior (requirements.md §4.1)
[x] R-31  RESOLVIDO — TVS 24 V + 470 uF/50 V na entrada de 12 V (bom_schematic.md itens 27-28)
[ ] R-29  Fechar a divergência do .fzz à mão, ou aceitar a convenção
[x] R-20  RESOLVIDO — TYPE=5 é Radar Móvel; hipótese de trecho controlado descartada

LACUNAS
[ ] L-01  Comportamento sem fix de GPS
[ ] L-02  Orçamentos de memória, corrente e tempo de boot
[ ] L-03  Falha ou ausência do cartão SD
[ ] L-04  Watchdog e recuperação
[ ] L-05  Faixa térmica (capacitor 105 °C)
[ ] L-06  Conversão e suavização da velocidade
[ ] L-07  Estratégia de verificação e testes
[ ] L-08  Linguagem e runtime declarados (C/C++ SDK recomendado; ver §9)
[ ] L-09  Precisão dos cálculos geográficos definida (FPU é single-precision)

EDITORIAIS
[ ] E-01 a E-05

PENDÊNCIAS DE formato_dados.md
[x] A-01  RESOLVIDO — layout e semântica confirmados      (= R-18)
[ ] A-02  Runtime decidido                                (= L-08)
[ ] A-03  Estimativas de memória medidas no binário real   (→ L-02)
```
