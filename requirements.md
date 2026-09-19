# 📑 Documentação de Requisitos e Interações Homem-Máquina (IHM)

**Projeto:** Detector de Radares GPS Inteligente
**Plataforma:** Raspberry Pi Pico 2 W (RP2350) — **C++17 com Pico SDK**
**Revisão:** 2 — 2026-09-15
**Base de dados:** 18.294 pontos (`base_igo8.txt` → `radares.bin`)

> **Revisão 2:** incorpora as correções de `revisao_tecnica.md` e as decisões de
> `formato_dados.md`. Rastreabilidade ao final do documento. Itens marcados
> **[SUPOSIÇÃO]** são recomendações aplicadas na ausência de decisão explícita —
> revisar e confirmar ou reverter.

## 🎯 1. Escopo e Objetivos do Sistema

O objetivo do sistema é atuar como um assistente de condução inteligente embarcado. Ele
deve calcular a telemetria do veículo via satélite (coordenadas, velocidade e rumo) e
confrontar esses dados em tempo real com uma base local de radares e semáforos para
prever a aproximação dos pontos de interesse e emitir alertas visuais, periféricos e
sonoros de forma contextualizada.

A base de origem é distribuída no padrão iGO8 (`base_igo8.txt`) e é convertida em um
formato binário compacto (`radares.bin`) **fora do dispositivo**, conforme RF06.

---

## 📋 2. Requisitos Funcionais (RF)

### [RF01] Telemetria em Tempo Real

O sistema deve ler os dados brutos NMEA do módulo GPS NEO-M8N a uma taxa de amostragem
**nominal de 4 Hz, com piso aceitável de 3 Hz** *(ver RF01.4)*, extraindo:

* Latitude e Longitude em graus decimais.
* Velocidade atual convertida para km/h.
* Rumo (azimute) do veículo em graus (0,0° a 359,9°).

#### [RF01.1] Sentença e validação

* O parser deve aceitar **`$GNRMC` e `$GPRMC`**. O talker `GN` só aparece com
  multi-GNSS ativo; em modo GPS-only a sentença vira `GP` e um parser fixado em `GN`
  não casaria nada — falha silenciosa total.
* Toda sentença deve ter o **checksum NMEA validado** antes do uso. Sentença com
  checksum inválido é descartada silenciosamente e não atualiza a telemetria.
* O campo de status da RMC (`A` = válido / `V` = alerta) deve ser verificado; sem fix
  válido, aplica-se o RF07.

#### [RF01.2] Configuração obrigatória do módulo

Configuração de fábrica, conforme datasheet UBX-15031086 (Tabela 15): **UART a 9600 bps,
8 bits, sem paridade, 1 stop bit, autobauding desabilitado**, com **sete** sentenças NMEA
ativas — `GGA, GLL, GSA, GSV, RMC, VTG, TXT`. A GSV sozinha gera múltiplas sentenças
quando há muitos satélites visíveis, e o total por época passa facilmente de 500 bytes.
A 9600 bps (960 B/s), **nem 1 Hz cabe com folga** e 4 Hz é impossível.

O firmware deve enviar, e reenviar a cada boot:

| Comando UBX | Ação |
| :--- | :--- |
| `CFG-RATE` | período de medição = **250 ms (4 Hz)** — ver RF01.4 |
| `CFG-PRT` | baud rate da UART = **115200 bps** |
| `CFG-MSG` | desabilitar todas as sentenças **exceto RMC** |
| `CFG-CFG` | persistir configuração (BBR e EEPROM da placa, se presente) |

#### [RF01.4] Modo GNSS e taxa de navegação

O datasheet do NEO-M8N (UBX-15031086, Tabela 1) limita a taxa máxima de navegação
conforme o modo de GNSS:

| Modo GNSS | Taxa máx. | Precisão horiz. | Cold start | Talker NMEA |
| :--- | :---: | :--- | :---: | :---: |
| **GPS + GLONASS** *(padrão de fábrica)* | **5 Hz** | 2,5 m · 2,0 m c/ SBAS | 26 s | `$GNRMC` |
| GPS apenas | 10 Hz | 2,5 m · 2,0 m c/ SBAS | 29 s | `$GPRMC` |

Os 5 Hz da especificação original eram **exatamente o teto** do módulo no modo padrão,
sem nenhuma folga: qualquer degradação por temperatura ou sinal fraco derrubaria a taxa
efetiva abaixo do requisito, sem aviso.

##### ✅ Configuração adotada (confirmada em 2026-09-15)

| Parâmetro | Valor |
| :--- | :--- |
| Modo GNSS | **GPS + GLONASS concorrente** (mantém o padrão de fábrica) |
| Taxa nominal | **4 Hz** (`CFG-RATE` = 250 ms) — 20% de folga sob o teto |
| **Piso aceitável** | **3 Hz** — abaixo disso é falha (RF01.5) |

##### Justificativa geométrica da banda

Deslocamento do veículo por amostra, contra o raio de alerta de 300 m:

| Velocidade | 4 Hz | 3 Hz | Pior caso como % do raio |
| :--- | ---: | ---: | ---: |
| 100 km/h (27,8 m/s) | 6,9 m | 9,3 m | 3,1% |
| **120 km/h** (33,3 m/s — maior limite da base) | 8,3 m | **11,1 m** | **3,7%** |

O custo do piso de 3 Hz é **no máximo um período de amostragem de antecedência**, ou
seja 333 ms — que a 120 km/h são 11 m dos 300 m de raio. O alerta efetivo cairia de
300 m para ~289 m no pior caso, contra uma janela de aviso de 9,0 s a essa velocidade.
Irrelevante para a função.

Manter a recepção concorrente preserva a disponibilidade de satélites em cânion urbano,
que é onde está a maior parte dos pontos da base.

> *Alternativa registrada:* se em uso real houver perda de fix recorrente em cidade,
> GPS-apenas tem **precisão horizontal idêntica** (2,5 m) e teto de 10 Hz, ao custo de
> 3 s de cold start e 1 dBm de sensibilidade. Como o RF01.1 aceita os dois talkers, a
> troca não exige alteração no parser.

#### [RF01.5] Monitoramento da taxa de atualização

Definir um piso de 3 Hz só tem valor se o sistema souber quando o cruzou. O firmware
deve medir a taxa efetiva de fixes válidos em janela deslizante de 3 s e classificar:

| Faixa medida | Estado | Ação |
| :--- | :--- | :--- |
| ≥ 3,5 Hz | **nominal** | operação normal |
| 3,0 a 3,5 Hz | **degradado** | opera normalmente; registra o evento |
| **< 3,0 Hz** sustentado por 5 s | **falha** | ver RF07 e a tentativa de recuperação abaixo |

##### Por que este requisito existe

Taxa abaixo do nominal **quase nunca** significa que o receptor está com dificuldade — o
NEO-M8N entrega a taxa configurada enquanto tiver fix, ou perde o fix. As causas reais,
em ordem de probabilidade:

1. **`CFG-RATE` não foi aplicado** e o módulo continua no padrão de 1 Hz.
2. **UART saturada** — sentenças demais habilitadas ou baud rate baixo, com frames
   truncados (ver RF01.2).
3. Parser descartando sentenças por checksum inválido (ruído elétrico na linha).
4. Dificuldade genuína do receptor, em condições extremas.

Ou seja: **este monitor é primariamente uma verificação de que a configuração do RF01.2
pegou**, e só secundariamente um indicador de saúde. É a diferença entre descobrir um
erro de configuração na bancada e descobri-lo depois de meses rodando a 1 Hz sem notar.

##### Diagnóstico e recuperação

* Contar **separadamente** as sentenças recebidas com checksum inválido. Isso distingue
  "o GPS não está enviando" (causas 1, 2, 4) de "estamos falhando em interpretar"
  (causa 3) — dois problemas com correções opostas.
* Ao detectar estado de **falha**, reenviar a sequência de configuração UBX do RF01.2
  uma vez antes de escalar para o RF07. Cobre o caso de configuração perdida.
* A taxa medida e a contagem de checksums inválidos devem estar visíveis em algum ponto
  da interface (tela de diagnóstico ou tela de boot), não apenas em log.

#### [RF01.3] Conversão de velocidade

A sentença RMC entrega velocidade em **nós**. Conversão obrigatória:
`km/h = nós × 1,852`.

A velocidade usada na decisão de zona (RF03) deve passar por **média móvel de 3
amostras** para suprimir jitter. A latência resultante **varia com a taxa efetiva**:
750 ms a 4 Hz e 1000 ms no piso de 3 Hz.

Essa latência afeta apenas a comparação contra o limite da via (RF03). O **cálculo de
distância usa sempre o fix instantâneo**, nunca a média — a posição não deve ser
suavizada, ou o alerta atrasaria junto.

> *Referência de datasheet (Tabela 1):* precisão de velocidade de **0,05 m/s**
> (0,18 km/h) e de rumo de **0,3°**. A precisão de velocidade é excelente para a
> comparação contra o limite da via no RF03. A de rumo **não é qualificada por
> velocidade mínima** no datasheet, então o limiar de 5 km/h do RF02.3 permanece
> julgamento de engenharia, não especificação.

---

### [RF02] Processamento Geográfico Inteligente

O sistema deve calcular continuamente a distância entre o veículo e os pontos da base,
identificando o alvo válido mais próximo.

#### [RF02.1] Busca em dois estágios

Calcular a distância para todos os 18.294 pontos a cada ciclo não cabe no orçamento de CPU.
A base é mantida em RAM **ordenada por latitude crescente** (RF06) e a busca opera em
dois estágios:

1. **Busca binária** pela faixa de latitude `[lat − 300 m, lat + 300 m]` — a constante
   é 269 unidades de 10⁻⁵ grau. Custo: ~15 comparações de inteiros.
2. **Varredura da faixa** com descarte por longitude em **aritmética inteira**, sem
   trigonometria. O limiar de longitude depende de `cos(lat)` e deve ser calculado
   **uma vez por fix**, nunca por ponto.
3. **Cálculo de distância apenas nos sobreviventes.**

Custo medido sobre a base real, pior caso: 96 candidatos na faixa de latitude, dos quais
**24 chegam ao cálculo de distância**. Ver `formato_dados.md` §4.

> A ordenação por **latitude** e não longitude é deliberada: o grau de latitude mede
> 111,32 km em qualquer ponto do globo, então o limiar da busca binária é constante. Em
> longitude ele dependeria de cos(lat) e o índice ficaria distorcido de norte a sul.

#### [RF02.2] Cálculo de distância e precisão numérica

A FPU do RP2350 é de **precisão simples**. Um `float` tem ~7,2 dígitos significativos,
enquanto uma coordenada como `-23.537216` precisa de 8 — e a fórmula de Haversine
subtrai números próximos entre si, ampliando o erro justamente em distâncias curtas.

* **Obrigatório:** o cálculo deve operar sobre **diferenças** de coordenadas, não sobre
  valores absolutos em precisão simples.
* **Fórmula adotada — equirretangular:** erro inferior a 0,01% na faixa de 300 m, e
  custo de um `sqrtf` contra 2 sin + 2 cos + 1 asin + 1 sqrt da Haversine.

```
dy = Δlat × 111320
dx = Δlon × 111320 × cos(lat)
d  = √(dx² + dy²)
```

* Se a fórmula de Haversine for usada, deve ser em **`double`** (emulado em software,
  mas cabe no orçamento). Haversine em `float` sobre coordenadas absolutas é proibida.

#### [RF02.3] Filtro de Sentido (Rumo)

O sistema deve comparar o rumo do veículo com o rumo de captura do ponto, respeitando o
campo `DirType` da base (ver RF06):

| `DirType` | n na base | Regra |
| :---: | ---: | :--- |
| **0** — omnidirecional | 1.419 (7,8%) | **Nunca descartar** por rumo |
| **1** — unidirecional | 15.218 (83,2%) | Válido se a diferença circular ≤ **30°** |
| **2** — bidirecional | 1.657 (9,1%) | Válido no rumo indicado **e no oposto** |

* **Diferença circular obrigatória:** `min(|a−b|, 360−|a−b|)`. A diferença aritmética
  simples falha no wraparound — entre rumo 355° e ponto 5° ela dá 350° e descartaria um
  ponto válido.
* **Bidirecional** resolve-se dobrando a diferença para a faixa 0–90° em vez de 0–180°,
  sem caso especial no laço crítico.
* **Porta de velocidade:** abaixo de **5 km/h** o azimute do NEO-M8N é ruído aleatório.
  Nesse regime o filtro deve **abrir**, não fechar — descartar por um rumo que é ruído
  perderia pontos reais.

---

### [RF03] Alertas Contextuais (Zonamento)

O sistema deve alternar entre **4 zonas** de operação, com base no ponto alvo válido:

| Zona | Condição |
| :--- | :--- |
| **1. Zona Segura** | Nenhum ponto válido a ≤ 300 m |
| **2. Zona de Aproximação** | `limite > 0` **e** distância ≤ 300 m **e** `velocidade ≤ V_infra` — com dois sub-estados (RF03.9) |
| **3. Zona de Perigo** | `limite > 0` **e** distância ≤ 300 m **e** `velocidade > V_infra` |
| **4. Zona de Semáforo** | `limite == 0` **e** distância ≤ 300 m |

Onde **`V_infra`** é o limiar de infração definido no RF03.6 — **não** o limite da via.
A fronteira entre Aproximação e Perigo é o ponto em que a multa passa a existir, não o
ponto em que o limite é excedido.

#### [RF03.1] Condição de aproximação

Distância ≤ 300 m **não basta**. Após passar pelo ponto, a distância volta a crescer mas
permanece abaixo de 300 m por vários segundos — o alerta continuaria ativo e, na Zona de
Perigo, o buzzer seguiria tocando depois do radar. O alerta só é emitido se o ponto
estiver **à frente**:

* Comparar o azimute veículo→ponto com o rumo do veículo (diferença circular ≤ 90°), ou
* Exigir distância **decrescente** entre amostras consecutivas.
#### [RF03.2] Histerese

A troca de zona deve ter histerese para evitar disparos repetidos na fronteira:

* Entrada em zona de alerta: distância ≤ **300 m**.
* Saída para Zona Segura: distância > **340 m** ou ponto já ultrapassado.

Sem histerese, a zona alternaria em laço com a distância oscilando em torno do limiar.

Há uma **segunda histerese, independente desta**, nas fronteiras de velocidade: entre
Aproximação e Perigo, e entre as faixas sonoras do RF03.7. Ver RF03.7.
#### [RF03.3] Zona de Semáforo — `limite == 0`

**1.430 pontos da base (7,8%) têm limite 0** — são semáforos, que não têm limite de
velocidade aferível. A condição da Zona de Perigo (`velocidade > limite`) seria
satisfeita por **qualquer** velocidade acima de zero, fazendo cada semáforo disparar
fundo vermelho e buzzer em "metralhadora" até o carro parar.

Regras da Zona de Semáforo:

* O gatilho é **`limite == 0`**, e **não** `tipo == semáforo`. Os pontos de
  **Semáforo c/ Câmera** (`TYPE=3`) não aferem velocidade e portanto não têm limite —
  são estes os 1.430. Os 22 casos de `TYPE=3` com limite 50/60 são inconsistências da
  base e seguem pelo caminho normal de radar de velocidade.

> ⚠️ **Lacuna conhecida — `TYPE=2` é semáforo e não entra aqui.** Os **2.994 pontos
> de Semáforo c/ Radar** (`TYPE=2`) fiscalizam avanço de sinal **e** velocidade. Como
> têm limite, o gatilho por `limite == 0` não os pega, e o motorista não recebe
> nenhuma indicação de fiscalização de sinal em 2.994 cruzamentos urbanos. O
> zonamento pela velocidade está correto; o que falta é indicação visual combinada,
> usando o campo `TYPE` já preservado no registro. Ver **R-26** — decisão de forma
> deferida para a sessão de telas.
* **Nunca** entra em Zona de Perigo, em qualquer velocidade.
* **Buzzer permanentemente silencioso** nesta zona.
* Visor não exibe placa de limite — não há limite a exibir.
#### [RF03.4] Prioridade entre alertas simultâneos

Em área urbana densa, semáforo e radar de velocidade coexistem nos 300 m com frequência:
**29,2% dos semáforos da base têm radar de velocidade a menos de 300 m** (417 de 1.430).
A regra é exercitada com frequência, não é caso de borda.

##### Ordem de precedência (decisão do autor, 2026-09-15)

A precedência é **por categoria**, e só dentro de cada nível decide a distância:

| Nível | Estado | Ação do motorista | Por que nesta posição |
| :---: | :--- | :--- | :--- |
| **1** | 🔴 **Zona de Perigo** | **Frear, agora** | Única com ação certa e consequência financeira em curso. |
| **2** | 🩷 **Aproximação em margem** | **Não acelerar** | Há ação, e o aparelho sabe que ela é necessária. |
| **3** | 🟡🔴 **Zona de Semáforo** | Depende do sinal ao chegar | Há risco, mas o aparelho não vê o semáforo. |
| **4** | 🟡 **Aproximação conforme** | **Nenhuma** | Único estado sem ação associada. Cede a vez. |

##### Por que a margem fica acima do semáforo

A justificativa original da precedência por categoria era que *"a Aproximação é o único
estado em que a informação não tem ação associada"*. Com o RF03.9, isso passou a valer
**apenas para o sub-estado conforme**. O sub-estado de margem tem ação clara — não
acelerar — e o aparelho tem certeza de que ela se aplica, porque comparou a velocidade
medida com `V_infra`.

Sem esta distinção, um semáforo em qualquer ponto da janela de 300 m suprimiria o aviso
de rosa piscante, e o motorista a 1 km/h da multa não saberia. Como a coexistência
ocorre em 29% das aproximações de semáforo, o aviso de margem seria perdido justamente
nas vias urbanas onde ele mais importa.

##### Implementação

A varredura do RF02.1 deve rastrear **quatro candidatos**, o mais próximo de cada
categoria, em vez de um único alvo:

```
mais_proximo_perigo     — vel > V_infra
mais_proximo_margem     — limite < vel <= V_infra
mais_proximo_semaforo   — limite == 0
mais_proximo_conforme   — vel <= limite
```

O estado exibido é o de menor nível não vazio. Custo: quatro ponteiros em vez de um, sem
impacto de desempenho.

> ⚠️ **O estado é por ponto, não global.** Dois radares na janela podem ter limites
> diferentes: um de 60 km/h a 250 m e um de 80 km/h a 100 m, com o veículo a 70 km/h,
> produzem simultaneamente um candidato de Perigo (o de 60, mais distante) e um de
> conforme (o de 80, mais próximo). Avaliar o estado **do radar mais próximo** e usá-lo
> como estado do sistema seria errado — daí os quatro ponteiros.
#### [RF03.5] Radar Móvel

2.065 pontos da base (11,3%) são **Radar Móvel** (`TYPE=5`): locais onde a
fiscalização móvel é sabidamente operada, com limites de 60 a 120 km/h — faixa
rodoviária, sem nenhum valor urbano.

##### Tratamento

Zonamento **idêntico a radar de velocidade fixo**: `V_infra`, sub-estados, faixas
sonoras e LED conforme RF03.6 a RF03.9. Radar móvel afere velocidade instantânea como
qualquer radar pontual, então não há diferença de lógica.

##### Consideração para a sessão de telas

Um ponto de radar móvel é **probabilístico**: marca onde a fiscalização *costuma*
ocorrer, não onde ela está agora. Um radar fixo sempre está lá; um móvel pode não
estar. Isso é diferença de confiança, não de zonamento, e sugere distinguir os dois na
tela — o alerta é o mesmo, a certeza não.

> 🎨 Decisão de forma visual deferida para a sessão dedicada a telas e interfaces.

> ⚠️ **Histórico:** este requisito descrevia "trecho controlado" com aferição de
> velocidade média. A hipótese veio de inferência sobre o perfil de velocidade e foi
> **descartada** em 2026-09-16, quando o significado real dos códigos `TYPE`
> identificou `TYPE=5` como Radar Móvel. Ver `formato_dados.md` §0.2 e §7.2.

#### [RF03.6] Limiar de infração e margem de segurança

O sistema não alerta com base no limite da via, mas no **limiar em que a multa passa a
existir** — descontado de uma **margem de segurança deliberada**.

##### Valores legais (confirmados pelo autor na Resolução CONTRAN)

A velocidade considerada para fins de punição é a medida **menos** o desconto:

| Limite da via | Desconto legal |
| :--- | :--- |
| ≤ 100 km/h | **7 km/h** absolutos |
| > 100 km/h | **7%** do limite |

*Exemplo:* um veículo a 87 km/h numa via de 80 km/h tem velocidade considerada de
80 km/h — não é punido.

##### Valores de projeto (com margem)

O firmware usa valores **mais conservadores** que os legais, por decisão de projeto:

| Limite da via | Desconto adotado | `V_infra` |
| :--- | :--- | :--- |
| ≤ 100 km/h | **6 km/h** *(1 km/h de margem)* | `limite + 6` |
| > 100 km/h | **5%** *(2 pontos percentuais de margem)* | `limite × 1,05` |

> ⚠️ **Estes valores são intencionalmente diferentes dos legais. Não "corrigir".**
> A diferença é margem de segurança contra erro de medição do próprio GPS (precisão de
> velocidade de 0,05 m/s pelo datasheet, mas sem garantia sob multipath urbano) e contra
> variação de aferição do radar. Os quatro números — 7, 7%, 6 e 5% — devem ser constantes
> de configuração nomeadas, com o par legal e o par de projeto separados, para que a
> margem seja ajustável sem tocar na regra legal.

Limiares resultantes para os dez limites da base:

| Limite | `V_legal` | **`V_infra` (projeto)** | Margem |
| :--- | ---: | ---: | ---: |
| 30 | 37,0 | **36,0** | 1,0 |
| 40 | 47,0 | **46,0** | 1,0 |
| 50 | 57,0 | **56,0** | 1,0 |
| 60 | 67,0 | **66,0** | 1,0 |
| 70 | 77,0 | **76,0** | 1,0 |
| 80 | 87,0 | **86,0** | 1,0 |
| 90 | 97,0 | **96,0** | 1,0 |
| 100 | 107,0 | **106,0** | 1,0 |
| 110 | 117,7 | **115,5** | 2,2 |
| 120 | 128,4 | **126,0** | 2,4 |

##### Fronteira em 100 km/h

Com os valores **legais** a função é contínua em 100 km/h (7 km/h = 7% de 100). Com os
valores **de projeto** há uma pequena descontinuidade — 6 km/h em 100, contra 5,05 km/h
em 101 — sempre no sentido conservador. Não requer tratamento especial: a regra é
`limite ≤ 100 → +6`, senão `× 1,05`.
#### [RF03.7] Escalonamento sonoro da Zona de Perigo

A Zona de Perigo é a **única zona com sinalização sonora** (ver RF03.8). A frequência
dos bipes deve escalonar com a gravidade do excesso, em três faixas.

##### ⚠️ Ancoragem das faixas: `V_infra`, não o limite

A especificação original pedia faixas de "10% e 20% acima da velocidade da via". Medidas
a partir do **limite**, elas colidem com a tolerância do RF03.6, porque 7 km/h é um
percentual grande em via lenta: a 30 km/h equivale a 23%, já acima da própria faixa de
20%.

Resultado da ancoragem literal no limite:

| Limite | `V_infra` | `limite × 1,10` | Faixa 1 (lenta) |
| :--- | ---: | ---: | :--- |
| 30 | 36,0 | 33,0 | ❌ **vazia** |
| 40 | 46,0 | 44,0 | ❌ **vazia** |
| 50 | 56,0 | 55,0 | ❌ **vazia** |
| 60 | 66,0 | 66,0 | ❌ **vazia** |
| 70 | 76,0 | 77,0 | +1,0 km/h |
| 80 | 86,0 | 88,0 | +2,0 km/h |
| 120 | 126,0 | 132,0 | +6,0 km/h |

Em **~80% dos radares de velocidade da base** (13.429 de 16.864, os de limite
≤ 60 km/h), a faixa de bipes lentos seria vazia — e a 30 km/h até a faixa intermediária
seria vazia, fazendo o aparelho saltar direto para a faixa mais alta no instante em que
cruzasse o limiar. Exatamente o oposto do escalonamento pretendido, e nas vias onde o
aparelho mais será usado.

**Ancoragem adotada: os percentuais contam a partir de `V_infra`.**

| Faixa | Condição | Padrão sonoro |
| :---: | :--- | :--- |
| **1** | `V_infra` a `V_infra × 1,10` | bipe de 100 ms a cada **1000 ms** |
| **2** | `V_infra × 1,10` a `V_infra × 1,20` | bipe de 100 ms a cada **350 ms** |
| **3** | acima de `V_infra × 1,20` | bipe de 50 ms a cada **100 ms** — pulso rápido, **não contínuo** |

Faixas resultantes, em km/h (com os valores de projeto do RF03.6):

| Limite | Faixa 1 — lento | Faixa 2 — rápido | Faixa 3 — pulso rápido |
| :--- | :--- | :--- | :--- |
| 30 | 36,0 – 39,6 | 39,6 – 43,2 | > 43,2 |
| 40 | 46,0 – 50,6 | 50,6 – 55,2 | > 55,2 |
| 50 | 56,0 – 61,6 | 61,6 – 67,2 | > 67,2 |
| 60 | 66,0 – 72,6 | 72,6 – 79,2 | > 79,2 |
| 70 | 76,0 – 83,6 | 83,6 – 91,2 | > 91,2 |
| 80 | 86,0 – 94,6 | 94,6 – 103,2 | > 103,2 |
| 90 | 96,0 – 105,6 | 105,6 – 115,2 | > 115,2 |
| 100 | 106,0 – 116,6 | 116,6 – 127,2 | > 127,2 |
| 110 | 115,5 – 127,1 | 127,1 – 138,6 | > 138,6 |
| 120 | 126,0 – 138,6 | 138,6 – 151,2 | > 151,2 |

Todas as faixas são não vazias e monotônicas em todos os limites da base, e o
escalonamento **1 Hz → ~2,9 Hz → 10 Hz** é perceptivelmente distinguível.

##### ⚠️ Por que a faixa 3 não é contínua

A especificação anterior usava **bipe contínuo** na faixa 3. Alterado em 2026-09-18:
**tom contínuo é o padrão menos detectável contra ruído estável.** O sistema auditivo se
adapta a um tom constante em segundos, e ruído de vento faz exatamente isso com ele — o
alerta mais urgente teria a forma mais fácil de mascarar.

Um pulso rápido a 10 Hz mantém a mensagem de escalonamento pela cadência, resiste ao
mascaramento por ser transiente repetido, e ainda é inconfundível em relação aos ~2,9 Hz
da faixa 2. Ver **R-32** para a análise de audibilidade que motivou a mudança.

O buzzer é **piezo ativo** nos dois modelos aceitos (item 7 do BOM), então as três faixas
são geradas apenas ligando e desligando a alimentação — o tom é interno ao componente.
A 10 Hz o ciclo é de 100 ms, folgado para um timer de hardware.

##### Histerese de faixa

A troca de faixa exige **histerese de 2 km/h na descida**: sem ela, velocidade oscilando
sobre uma fronteira faria o padrão sonoro tremular. A faixa sobe imediatamente ao cruzar
o limiar e só desce 2 km/h abaixo dele.

##### Implementação

O padrão de bipes deve ser gerado por **timer de hardware**, conforme RNF04, nunca por
laço de espera — o laço crítico do GPS não pode ser bloqueado pelo buzzer.
#### [RF03.8] Sinalização sonora exclusiva da Zona de Perigo

**A Zona de Aproximação é silenciosa.** Os "2 bipes curtos na entrada da zona" da
especificação original foram removidos.

Consequência de projeto, e o principal ganho desta revisão: **o buzzer passa a ter um
único significado** — "você está sendo multado se não reduzir" — e sua frequência
codifica a gravidade. Nenhum outro estado do sistema emite som. Aproximação, Semáforo,
OTA, falha de dados e perda de fix são todos silenciosos.

Isso torna o alerta treinável: o motorista aprende **um** som e não precisa interpretar
qual dos estados o produziu. Um buzzer que também toca quando nada precisa ser feito
perde autoridade e passa a ser ruído.
#### [RF03.9] Sub-estados da Zona de Aproximação

A Zona de Aproximação cobre toda a faixa até `V_infra`, o que inclui velocidades **acima
do limite da via** mas ainda sem multa. Sem distinção interna, o motorista receberia
"tudo bem" a 1 km/h do limiar e a transição para o bipe seria um degrau sem aviso.

A zona é dividida em dois sub-estados, **ambos silenciosos**:

| Sub-estado | Condição | LED RGB | Significado |
| :--- | :--- | :--- | :--- |
| **Conforme** | `velocidade ≤ limite` | 🟡 **Amarelo fixo** | Dentro do limite. Nada a fazer. |
| **Margem** | `limite < velocidade ≤ V_infra` | 🩷 **Rosa piscante (1 Hz)** | Acima do limite, ainda sem multa. **Não acelere.** |

Largura da faixa de margem: **6,0 km/h** em todos os limites até 100 km/h, e 5,5 km/h em
110 (ver RF03.6).

##### Progressão de estados do LED

| Estado | Cor | Mistura RGB | Piscada |
| :--- | :--- | :--- | :--- |
| Zona Segura | 🟢 verde | G 100% | fixo |
| Aproximação conforme | 🟡 amarelo | R 100% + G ~70% | fixo |
| **Aproximação em margem** | 🩷 **rosa** | **R 100% + B ~40%** | **1 Hz** |
| Zona de Perigo | 🔴 vermelho | R 100% | 4 Hz |
| *(Zona de Semáforo)* | 🟡🔴 *alternado* | *amarelo ↔ vermelho* | *2 Hz* |

##### Por que rosa é mais robusto que laranja aqui

A escolha do rosa não é estética — é de **separação de canal**. Os estados adjacentes na
escala de gravidade são amarelo (R+G) e vermelho (R). Uma cor de aviso intermediária
precisa ser distinguível de ambos mesmo com o LED descalibrado:

| Candidata | Mistura | Falha se o canal secundário estiver fraco | Falha se estiver forte |
| :--- | :--- | :--- | :--- |
| Laranja | R + G ~40% | vira **vermelho** → confunde com Perigo | vira **amarelo** → confunde com conforme |
| **Rosa** | R + B ~40% | vira **vermelho** → confunde com Perigo | vira **magenta** → não colide com nada |

Laranja fica **no mesmo eixo de canal do amarelo**, diferindo apenas pela razão R:G — o
que a torna sensível nas duas direções. Rosa usa o canal **azul**, que nenhum estado de
condução utiliza, então difere do amarelo e do vermelho por *qual canal está aceso*, não
por uma proporção. Sobra apenas um modo de falha em vez de dois.

O canal azul é usado somente nos estados de boot, sem sinal e OTA — todos temporalmente
separados da condução, e o rosa é dominado pelo vermelho, sem risco de confusão com azul.

##### Contrapartida: o rosa não tem convenção de "atenção"

A rampa verde → amarelo → **laranja** → vermelho é convenção aprendida (trânsito, sinais
de risco). Verde → amarelo → **rosa** → vermelho não é. Para um aparelho de usuário único
isso se aprende em um dia de uso, e a robustez de leitura periférica vale mais que a
convenção. Registrado como escolha deliberada, não omissão.

> ⚠️ **Dependência do R-05.** Rosa exige o canal **azul** funcionando e calibrado. Com os
> 330 Ω da revisão 1 o azul fica abaixo de 1 mA e não acende, e o estado apareceria como
> **vermelho fixo piscando** — indistinguível do Perigo, o oposto da intenção. Os 68 Ω
> especificados no BOM cobrem verde e azul; a razão R:B deve ser calibrada por PWM com o
> LED real e as três cores verificadas lado a lado, em luz ambiente e sob sol direto.

##### Histerese

Mesma regra do RF03.7: **2 km/h na descida**. O sub-estado sobe imediatamente ao cruzar
o limite da via e só retorna a "conforme" 2 km/h abaixo dele.

##### O LED RGB sinaliza exclusivamente estado de via

Decisão do autor em 2026-09-15: os estados **de boot, sem sinal de GPS, OTA e falha de
dados** são exibidos **somente na tela**. O LED RGB fica apagado neles.

Isso reduz a paleta de nove estados para **cinco**, todos da mesma categoria semântica —
"qual é a sua situação em relação à via":

| Estado | LED RGB |
| :--- | :--- |
| Zona Segura | 🟢 verde fixo |
| Aproximação conforme | 🟡 amarelo fixo |
| Aproximação em margem | 🩷 rosa piscante (1 Hz) |
| Zona de Perigo | 🔴 vermelho piscante (4 Hz) |
| Zona de Semáforo | 🟡🔴 amarelo/vermelho alternados (2 Hz) |
| *(todos os demais)* | **apagado** |

**O LED apagado passa a ter significado próprio: nenhuma proteção ativa.** É
inequivocamente distinguível do verde fixo, então o motorista que olhe de relance sabe
que o sistema não está avaliando a via — durante o boot, sem fix, em OTA ou com a base
indisponível. Um LED colorido nesses estados diria "estou trabalhando" sem estar.

Consequência prática: o canal **azul** deixa de ser usado isoladamente e passa a servir
apenas à mistura do rosa (R+B).

> ✅ **O LED verde de status de Wi-Fi saiu do projeto** (decisão do autor, 2026-09-15).
> Com boot, sem sinal, OTA e falha de dados exibidos somente na tela, o indicador
> dedicado tornou-se redundante. Consequências:
>
> * O **LED RGB é o único indicador luminoso do projeto** e sinaliza exclusivamente
>   estado de via — cinco estados, uma categoria semântica, nenhuma exceção.
> * O **GPIO 14** fica livre, e a matriz IHM perdeu uma coluna inteira.
> * Todo estado de aparelho (por oposição a estado de via) vive na tela.

##### Histerese

Mesma regra do RF03.7: **2 km/h na descida**. O sub-estado sobe imediatamente ao cruzar
o limite da via e só retorna a "conforme" 2 km/h abaixo dele.

##### Nota de capacidade da paleta

O LED RGB acumula agora **nove estados** distintos (segura, conforme, margem, perigo,
semáforo, boot, sem sinal, OTA, falha de dados), distinguidos por hue e taxa de piscada.
Está próximo do limite do que um único LED difuso comunica de forma confiável na visão
periférica. Novos estados devem preferir a tela, não mais cores.
#### [RF03.10] Controle de velocidade média — ❌ **ANULADO**

> ❌ **Requisito anulado em 2026-09-16.** Não há dados sobre os quais operar.
>
> O requisito pressupunha que `TYPE=5` fosse trecho controlado, com pórticos de início
> e fim aferindo velocidade média. O significado real dos códigos `TYPE`
> (`formato_dados.md` §0.2) identificou `TYPE=5` como **Radar Móvel** — fiscalização
> pontual. **A base não contém nenhum trecho controlado**, e portanto não há par de
> pórticos a cronometrar.
>
> O desenho anterior (abrir o trecho no primeiro pórtico, fechar no seguinte; integrar
> a velocidade em vez das posições) fica registrado no histórico do arquivo, não aqui —
> não há razão para manter especificação de mecanismo que não tem entrada de dados.
>
> O número RF03.10 fica **retirado**, não reservado.

---

### [RF04] Ajuste de Luminosidade Dinâmico

O sistema deve ler o encoder rotativo KY-040 para ajustar o ciclo de trabalho
(*duty cycle*) do PWM do backlight em passos de aproximadamente **5% por clique**,
permitindo escurecer o visor para condução noturna.

* **Piso de brilho de 5%:** o ajuste não deve permitir 0% real. Com o visor totalmente
  apagado o usuário perde a referência visual para recuperá-lo.

#### ✅ Debounce: máquina de estados em software, filtro RC como contingência

*Revisado em 2026-09-19, sobre medição na placa. Ver **R-36**.*

A revisão 2 exigia **as duas coisas**: filtro RC de 100 nF em `CLK` e `DT` **mais**
decodificação por máquina de estados. Medido, o filtro de 100 nF **impedia qualquer
decodificação** — ele achatava fases de 45 a 128 ms em pulsos de 1 ms e apagava o estado
`(0,0)`, que é onde a quadratura codifica direção.

**O que o requisito exige agora:**

* **Decodificação por máquina de estados de quadratura, obrigatória.** Ela é o debounce.
  Repique de contato é oscilação entre dois estados adjacentes, e na tabela de transição
  isso soma `+1, −1, +1, −1` — **zero líquido**. Um passo só sai com **quatro transições
  válidas consecutivas na mesma direção**, o que ruído simétrico não produz.
* **Filtro RC: contingência, não requisito.** Se o brilho oscilar sozinho no veículo,
  solde **1 a 10 nF** em `CLK` e `DT`. Nunca 100 nF.

**Por que o filtro saiu do caminho crítico.** O filtro em software é **independente de
frequência**; o RC não é, e essa dependência foi exatamente o que quebrou. Medições que
sustentam a decisão:

| Evidência | Valor |
| :--- | :--- |
| Mudanças espúrias, parado, sem capacitor | **0 em 12 s** |
| Passos falsos na sessão de calibração | nenhum |
| Folga do `(0,0)` sobre a amostragem | **5,8×** |

**O que não foi testado:** o ambiente do veículo, com alternador e ignição. Dois fatos
reduzem o risco — o encoder é **interno ao gabinete**, com fios curtos, ao contrário do
buzzer, que é remoto; e o modo de falha é **benigno e óbvio**, o brilho saltando sozinho,
não uma falha silenciosa. **Manter o lugar dos capacitores na placa** é o que torna a
contingência barata.

#### Leitura por polling, não por interrupção

*Revisado em 2026-09-19. A revisão 2 exigia interrupção de hardware.*

A implementação usa **polling a 1 ms**, e a medição justifica: o estado `(0,0)` dura
**5,75 ms no pior caso**, já incluindo giro rápido — **5,8× de folga**. Nenhum detente se
perde.

A interrupção de borda continua sendo a saída caso o laço principal passe a bloquear por
mais de ~5 ms, e o desenho está pronto para ela: o `DecodificadorQuadratura` é função
pura que não sabe de onde vem a amostra. Ver `docs/adr/0005`.

---

### [RF05] Atualização Sem Fio (Over-The-Air)

Ao detectar o clique no eixo do encoder, o sistema deve ativar a interface de rede do
Pico 2 W, conectar ao ponto de acesso configurado e baixar a versão atualizada da base
de radares, salvando-a no cartão SD.

#### [RF05.1] Pré-condição de segurança

O eixo do encoder é fácil de pressionar sem intenção. Como o modo OTA suspende a leitura
do GPS, entrar nele em movimento deixaria o motorista sem alerta sem saber por quanto
tempo.

* O modo OTA só pode ser iniciado com o veículo **parado**: velocidade < 3 km/h por
  **3 segundos consecutivos**.
* Clique recebido fora dessa condição exibe aviso e é ignorado.
* **Timeout global de 120 s** para o modo OTA, com retorno automático ao velocímetro.

#### [RF05.2] Escrita atômica e validação

Queda de energia (motorista desliga o carro), timeout ou arquivo remoto corrompido não
podem deixar o dispositivo sem base — falha perigosa, porque a tela volta ao velocímetro
normalmente e nada indica a perda.

1. Baixar para `radares.tmp`.
2. **Validar** conforme RF06.3.
3. Renomear a base atual para `radares.bak`.
4. Renomear `radares.tmp` para `radares.bin`.
5. Em caso de falha em qualquer etapa: descartar o `.tmp` e manter a base vigente.
6. Se a base vigente falhar na validação no boot, carregar `radares.bak`.

* **Tentativas:** 3, com intervalo de 5 s.
* Resposta HTTP diferente de 200, `Content-Length` ausente ou arquivo vazio abortam a
  operação sem tocar na base vigente.

#### [RF05.3] Credenciais e integridade

* As credenciais de Wi-Fi **não podem estar embutidas no firmware**. Devem ser lidas em
  tempo de execução de arquivo de configuração no cartão SD (`wifi.cfg`).
* O cartão SD circula fora do ambiente de desenvolvimento — não usar rede corporativa
  nem credenciais reutilizadas para o AP de atualização.
* O download deve usar **HTTPS** ou verificação de assinatura na origem.

---

### [RF06] Base de Dados de Radares

#### [RF06.1] Conversão fora do dispositivo

A base de pontos é um CSV no **padrão iGO8**, com cabeçalho explícito:

```
X,Y,TYPE,SPEED,DirType,Direction
```

> ⚠️ **A ordem `X,Y` tem longitude primeiro.** Trocar as duas não gera erro algum: as
> coordenadas caem no oceano Índico e o sistema simplesmente nunca detecta nada. É a
> armadilha clássica deste formato.

A conversão para o formato binário é feita por **`converte.py`, fora do dispositivo**.
Parsear 612 KB de texto e ordenar 18 mil registros no RP2350 levaria minutos e
desgastaria o cartão, sem ganho algum — o arquivo é idêntico para todos os
dispositivos.

**Entrada em outro formato?** Escreva seu próprio parser. O requisito é apenas produzir
`radares.bin` conforme RF06.2 e RF06.3. Obter a base está fora do escopo deste
documento: o projeto não presume fonte nenhuma.

A base de referência usada no desenvolvimento tem **18.294 pontos** — radares fixos,
radares móveis e semáforos com e sem aferição de velocidade. Ver `formato_dados.md`
§0.2 para o significado de cada `TYPE`.

#### [RF06.2] Formato e carga

* Arquivo `radares.bin`: cabeçalho de 16 B + `n` registros de 12 B, **ordenados por
  latitude crescente**. Tamanho atual: 214,4 KB.
* A base é carregada **integralmente em RAM no boot** e não há acesso ao cartão SD
  durante a condução.
* Registro: `lat` e `lon` em `int32` (graus × 10⁵), `limite` em km/h (`uint8`, 0 = sem
  limite aferível), `rumo` em passos de 2° (`uint8`), e `flags` com `DirType` e tipo.
* Especificação completa em `formato_dados.md` §2 e §3.

#### [RF06.3] Validação obrigatória na carga

Nesta ordem, abortando na primeira falha:

1. Magic `"RDR1"`, versão e escala esperadas.
2. `tam_registro == 12`.
3. `n_pontos` entre 1 e **40.000** (teto de RAM — acima disso a carga integral estoura a
   memória e o arquivo deve ser recusado).
4. `16 + n_pontos × 12 == tamanho do arquivo`.
5. **CRC-32** do bloco de dados.
6. **Invariante de ordenação por latitude.**

> O item 6 não é opcional. A busca binária do RF02.1 retorna resultados silenciosamente
> errados se a base vier desordenada — falha sem sintoma.

---

### [RF07] Degradação Segura

Comportamento obrigatório nas condições de falha:

| Condição | Comportamento |
| :--- | :--- |
| **Sem fix de GPS** (túnel, garagem, cold start) | Visor indica "sem sinal" com contagem do tempo decorrido. Alertas suspensos. Último alvo é descartado após **10 s** sem fix — nunca manter alerta com posição obsoleta. |
| **Cartão SD ausente** (`DET` indica soquete vazio) | Visor indica **"insira o cartão"** — falha acionável pelo motorista. Opera como velocímetro simples. |
| **Cartão presente mas ilegível** | Visor indica **"cartão ilegível"** — falha que o motorista não resolve dirigindo. Opera como velocímetro simples. Não travar nem reiniciar em laço. |
| **`radares.bin` inválido** | Tentar `radares.bak`. Se ambos falharem, tratar como cartão ilegível. |
| **Travamento do firmware** | **Watchdog de hardware** com janela de 2 s. O motorista não pode reiniciar o dispositivo dirigindo. |
| **Cold start do GPS** | TTFF de **26 s** em GPS+GLONASS (datasheet, Tabela 1); hot start 1 s, aided 2 s. A tela de boot deve deixar explícito que ainda não há proteção ativa. |
| **Taxa abaixo de 3 Hz** sustentada (RF01.5) | Reenviar a configuração UBX uma vez. Se persistir, exibir aviso de degradação e **manter os alertas ativos** — 1 Hz ainda protege melhor que nada, mas o motorista precisa saber que a precisão caiu. |

---

## ⚙️ 3. Requisitos Não-Funcionais (RNF)

* **[RNF01] Robustez Elétrica:** a entrada de 5 V, proveniente do **conversor CC
  12 V → 5 V externo ao gabinete**, deve ser aplicada em **`VSYS` (pino 39)** através de
  **diodo Schottky em série**, e **não** em
  `VBUS` (pino 40). `VBUS` é ligado diretamente ao conector USB do Pico, e injetar 5 V
  externo nesse nó cria conflito de fontes quando o USB é usado para gravação ou debug.
  O circuito deve possuir filtragem capacitiva dupla (eletrolítico de 470 µF a 1000 µF
  **de 105 °C** + cerâmico de 100 nF) acoplada nesse nó, o mais próximo fisicamente
  possível dos pinos 39 e 38, tolerando transientes de ignição e ruído do alternador.

* **[RNF02] Isolamento de Portas:** o pino GPIO de sinal do buzzer não deve fornecer
  corrente direta ao componente; o acionamento deve ser feito através de um transistor
  NPN **atuando** como chave, com margem de corrente de pelo menos 5× o consumo do
  buzzer.

* **[RNF03] Armazenamento:** o sistema deve suportar cartões formatados estritamente em
  **FAT32**, e a comunicação SPI com o leitor deve operar em lógica nativa de **3,3 V**.
  Cartão com formatação diferente, ausente ou ilegível cai no RF07.

  **Detecção de presença:** o pino `DET` do leitor é ligado ao **GPIO 14**, permitindo
  distinguir cartão ausente de cartão ilegível (RF07).

  | Estado | Nível em GPIO 14 |
  | :--- | :--- |
  | Cartão inserido | **ALTO** (pull-up de 4,7 kΩ da própria placa) |
  | Sem cartão | **BAIXO** (ligado ao GND internamente) |

  * **Configurar o GPIO 14 como entrada sem pull.** A placa já fornece o pull-up de
    4,7 kΩ; o interno do Pico seria redundante e mais fraco.
  * O `DET` é **indício, não autoridade**. A tentativa de leitura continua sendo o
    veredito: se `DET` diz "presente" e a leitura falha, o estado é "ilegível". O `DET`
    só melhora a mensagem, nunca substitui a verificação.

* **[RNF04] Concorrência de Software:** a leitura do encoder (giro e clique) e o piscar
  dos LEDs devem ser tratados via **interrupções (IRQs)** e **timers de hardware**,
  garantindo que processos de interface não atrasem o laço crítico do GPS.

  Divisão obrigatória entre os núcleos do RP2350:

  | Núcleo | Responsabilidade |
  | :--- | :--- |
  | **Core 0** | UART do GPS, parsing NMEA, busca geográfica, máquina de estados de zona |
  | **Core 1** | Renderização e transferência SPI do display, LEDs, encoder |

  O barramento SPI0 é compartilhado entre display e cartão SD e exige **mutex**: nenhum
  acesso pode se intercalar com o outro. O cartão exige clock ≤ 400 kHz **na
  inicialização**, enquanto o display opera em dezenas de MHz — a velocidade deve ser
  reconfigurada por dispositivo antes de cada transação.

* **[RNF05] Modularidade Mecânica:** o buzzer deve ser montado **externamente ao
  gabinete**, conectando-se a ele por **JST-XH de 2 vias** — polarizado, de liberação
  rápida e sem contato deslizante. Conectores de áudio do tipo P2 estão **proibidos**
  neste circuito: a inserção faz o sleeve varrer o tip, curto-circuitando 5 V ao coletor
  do transistor sem o buzzer limitando a corrente. *(Decisão confirmada em 2026-09-15.)*

  ⚠️ **A posição não é detalhe de acabamento — é requisito acústico (R-32).** O caso de
  projeto é **janela aberta a 80 km/h**, com ruído interno da ordem de 87 dB(A), e nessa
  condição reposicionar e reorientar o buzzer valem **mais decibel do que trocar de
  modelo**. Portanto: **apontado para o motorista**, não para dentro do painel, e o mais
  próximo possível dele — coluna de direção ou base do para-brisa, não enterrado no
  console. "Escondido sob o painel", como dizia a revisão 2, era o oposto do que a
  acústica pede.

  O JST-XH torna o buzzer **trocável com o projeto fechado**, então o modelo é decisão
  reversível; a posição, uma vez fixada, não é.

* **[RNF06] Plataforma de Software:** o firmware deve ser implementado em **C++17 com o
  Raspberry Pi Pico SDK**. A escolha é condicionada por dois fatores:
  1. **Paralelismo real entre núcleos.** O RNF04 exige divisão de trabalho entre core 0
     e core 1; `multicore_launch_core1()` do SDK entrega isso. O `_thread` do
     MicroPython no port RP2 tem GIL e não executaria bytecode em paralelo.
  2. **Orçamento de memória.** A base de 214 KB mais o framebuffer de 150 KB não
     conviveriam com o interpretador e a heap do MicroPython em 520 KB de SRAM.

  Comparação completa em `formato_dados.md` §9.

* **[RNF07] Orçamento de Memória:** consumo alvo em 520 KB de SRAM:

  | Consumidor | Limite |
  | :--- | ---: |
  | Base de radares (estática em `.bss`) | 214 KB — teto de 40.000 registros |
  | Framebuffer 320×240 RGB565 | 150,0 KB |
  | Pilha lwIP + driver CYW43 | ~48 KB *(medir)* |
  | Stacks dos dois núcleos | ~8 KB |
  | Buffers de SD e UART | ~4 KB |
  | `.data` / `.bss` restante | ~20 KB |
  | **Reserva livre mínima** | **≥ 60 KB** |

  Alavanca de alívio prevista, se o limite for excedido: **renderização em bandas** de
  40 linhas (25,0 KB em vez de 150,0 KB), liberando **125 KB**. Com o display de 2,4" a
  reserva livre sem bandas cai a ~76 KB, abaixo do mínimo de 60 KB apenas por margem de
  estimativa — **as bandas deixaram de ser alavanca opcional e passaram a ser o desenho
  esperado**. O consumo real deve ser medido com `arm-none-eabi-size` e marca d'água de
  stack, não estimado.

* **[RNF08] Precisão Numérica:** a FPU do RP2350 é de **precisão simples**. Os cálculos
  geográficos devem seguir o RF02.2 — operar sobre diferenças de coordenadas, nunca
  sobre valores absolutos em `float`.

* **[RNF09] Faixa Térmica:** o painel de um veículo exposto ao sol ultrapassa 60 °C.
  Todos os componentes devem ser especificados para operação até **85 °C**, e o
  capacitor eletrolítico para **105 °C**.

  ⚠️ **O módulo GPS é o componente térmico limitante do projeto.** O datasheet
  UBX-15031086 (Tabelas 9 e 10) especifica para o NEO-M8N temperatura de operação de
  −40 a **+85 °C** e de **armazenamento igualmente limitada a +85 °C** — diferente do
  NEO-M8M, que vai a 105 °C de armazenamento. Não há margem acima de 85 °C nem com o
  aparelho desligado, e um painel fechado ao sol no verão brasileiro chega perto disso.
  Considerar montagem que evite incidência solar direta sobre o módulo.

* **[RNF10] Verificação:** antes de considerar uma funcionalidade concluída:
  * **Testes unitários** do parser NMEA (incluindo checksum inválido, campos vazios,
    sentença sem fix, talker `GN` e `GP`).
  * **Testes unitários** do cálculo de distância e do filtro de rumo com vetores
    conhecidos — **obrigatoriamente incluindo o caso de wraparound 355°/5°** e os três
    valores de `DirType`.
  * **Replay de logs NMEA** gravados em estrada, validando as transições de zona sem
    necessidade de dirigir.
  * **Teste de interrupção de energia** durante o OTA, validando o RF05.2.
  * O `radares.bin` gerado serve de *fixture* para os testes de busca.

---

## 👤 4. Matriz de Interação Homem-Máquina (IHM)

Esta tabela define como as ações do usuário (motorista) ou do ambiente geram respostas
nos atuadores visuais e sonoros do dispositivo.

| Evento / Ação do Usuário | Estado do Sistema | Visor IPS 2,4" 320×240 (ver §4.1) | LED RGB Periférico | Buzzer SFM-27 (Painel) |
| :--- | :--- | :--- | :--- | :--- |
| **Girar chave do carro (Boot)** | Inicialização | Exibe logo e contagem de pontos carregados. Indica que a proteção ainda não está ativa. | **Apagado** | Silencioso |
| **Aguardando primeiro fix** | Sem Sinal | "Buscando satélites" com tempo decorrido. | **Apagado** | Silencioso |
| **Dirigindo sem pontos próximos** | Zona Segura | Velocidade em branco, **sem denominador** — fora do raio de um ponto o aparelho não sabe o limite da via. Sem ícone e sem barra: a faixa inferior fica vazia. | **Verde Fixo** | Silencioso |
| **Entrou no raio de 300m, dentro do limite** (`vel ≤ limite`) | Aproximação — conforme | `velocidade/limite` em branco; ícone do tipo de ponto e barra de proximidade em **âmbar**. | **Amarelo Fixo** | **Silencioso** |
| **Acima do limite, ainda sem multa** (`limite < vel ≤ V_infra`) | Aproximação — **margem** | Idem, com a barra de proximidade em **rosa**. | 🩷 **Rosa Piscante (1 Hz)** | **Silencioso** |
| **Acima do limiar de infração** — faixa 1 (`V_infra` a +10%) | Zona de Perigo | Barra de proximidade em **vermelho**. Layout inalterado, **nada pisca na tela** (§4.1). | **Vermelho Piscante (4 Hz)** | Bipe de 100 ms a cada **1000 ms** |
| **Acima do limiar de infração** — faixa 2 (+10% a +20%) | Zona de Perigo | Idem. | **Vermelho Piscante (4 Hz)** | Bipe de 100 ms a cada **350 ms** |
| **Acima do limiar de infração** — faixa 3 (acima de +20%) | Zona de Perigo | Idem. | **Vermelho Piscante (4 Hz)** | Bipe de 50 ms a cada **100 ms** — pulso rápido, não contínuo (R-32) |
| **Entrou no raio de 300m de semáforo** | Zona de Semáforo | Ícone 🚦 e barra em **âmbar**. **Sem denominador** — não há limite a comparar. Para `TYPE=2` (semáforo *com* radar) o ícone é 🚦+🏎 e **há** denominador: resolve o **R-26**. | **Amarelo/Vermelho alternados (2 Hz)** | **Silencioso — sem exceção.** |
| **Entrou no raio de 300m de radar móvel** | Idêntico a radar fixo | Ícone 🏎 **vazado** em vez de preenchido: confiança expressa como preenchimento, não como símbolo novo — a fiscalização pode não estar ativa. Zonamento e LED idênticos a radar fixo. | conforme o estado da via | conforme o estado da via |
| **Perda de sinal em movimento** | Sem Sinal | Numerador vira **`- -`** — o GPS é a única fonte de velocidade. Faixa inferior: `SEM SINAL — alertas suspensos` com tempo decorrido, em **cinza**. Layout preservado. | **Apagado** | Silencioso |
| **Girar o botão do Encoder** | Ajuste de Brilho | Barra de brilho por ~1,5 s **na faixa superior**, deslocando o relógio. Curva perceptual, não linear (§4.1). | Mantém estado atual | Silencioso |
| **Clique do Encoder com carro em movimento** | Recusa | `PARE O VEÍCULO PARA ATUALIZAR` por 2 s **na faixa inferior**, não em tela cheia — os alertas continuam visíveis. | Mantém estado atual | Silencioso |
| **Clique do Encoder com carro parado** | Sincronismo OTA | Altera tela para "Atualizando base de dados...". | **Apagado** | Silencioso |
| **Wi-Fi autenticado e baixando arquivo** | Transferência | Barra de progresso ou animação de download. | **Apagado** | Silencioso |
| **Fim do download / Falha no Timeout** | Conclusão | Exibe "Sucesso!" ou "Falha na conexão — base anterior mantida". Retorna ao velocímetro após 2s. | Restaura estado da via | Silencioso |
| **Taxa de GPS abaixo de 3 Hz** | Degradação | Indicador discreto de taxa reduzida, sem ocultar o velocímetro. Alertas seguem ativos. | Mantém estado da via | Silencioso |
| **Cartão SD ausente ou base inválida** | Falha de Dados | `⚠ BASE INDISPONÍVEL — sem alertas` **permanente** na faixa inferior, em **cinza**, não 3 s. Sem base não há ponto, logo a barra de proximidade nunca teria o que mostrar: a faixa fica livre. Corrige o **R-30**. | **Apagado** | Silencioso |

---

### 4.1 Desenho de tela — geometria, canais e paleta

> Definido em 2026-09-17, na sessão dedicada a telas. Fecha o **R-26** e a distinção
> visual do radar móvel (RF03.5), ambos deferidos, e origina o **R-30**.

#### Módulo e geometria

| Grandeza | Valor |
| :--- | :--- |
| Painel | 2,4", **320×240**, paisagem, montado à esquerda do painel do veículo |
| Área ativa | 48,8 × 36,6 mm → **6,56 px/mm** |
| Dígito no pior caso (`120/110`) | 79 px = **12,0 mm** = 59 arcmin a 70 cm |

Referência de legibilidade: ~25 arcmin é o mínimo de placa de trânsito, 45+ é
confortável. O painel de 2,4" tem **pixels maiores** que um 1,3" de 240×240 (6,56 contra
10,28 px/mm), e é isso que torna o formato `velocidade/limite` legível de viés.

#### Divisão vertical (240 px)

| Região | Altura | Conteúdo |
| :--- | ---: | :--- |
| Fio de moldura | 2 px | decorativo, cor fixa |
| **Faixa superior** | 26 px | status do aparelho (ver inquilinato) |
| **Área do número** | 166 px | `velocidade/limite` |
| **Faixa inferior** | 44 px | ícone + barra, ou a razão de não haver alerta |
| Fio de moldura | 2 px | |

#### Quatro canais ortogonais

A informação se divide em quatro canais que **não se duplicam**. Cada um responde uma
pergunta diferente, e é isso que permite quatro estados de via, quatro tipos de ponto e
três estados degradados sem ambiguidade:

| Canal | Responde | Valores |
| :--- | :--- | :--- |
| **Número** | a que velocidade eu vou | `75` · `75/110` · `- -` |
| **Denominador** | há limite a comparar | presente / ausente |
| **Ícone** | que tipo de ponto vem | 🏎 cheio · 🏎 vazado · 🚦 · 🚦+🏎 |
| **Barra** | quão perto, e quão grave | preenchimento + cor |

A **moldura não é canal**: cor fixa, decorativa. O LED RGB periférico continua sendo o
canal de estado de via, e **nada na tela pisca** — a tela é o canal estável, o LED é o
canal pulsante. Trocar o layout no instante de maior estresse obrigaria o motorista a
reaprender a tela com 9 segundos de aviso a 120 km/h.

#### Número e denominador

Fora do raio de um ponto, **o aparelho não conhece o limite da via**: a base é um
conjunto de pontos, não uma malha viária, e `limite` é o limite *daquele radar*. O
denominador portanto só existe perto de um ponto que afira velocidade:

| Situação | Número | Ícone | Barra |
| :--- | :--- | :--- | :--- |
| Zona Segura | `75` | — | — |
| Radar fixo (`TYPE=1`) | `75/110` | 🏎 cheio | ✓ |
| Radar móvel (`TYPE=5`) | `75/110` | 🏎 **vazado** | ✓ |
| Semáforo c/ radar (`TYPE=2`) | `75/60` | 🚦 **+** 🏎 | ✓ |
| Semáforo c/ câmera (`TYPE=3`) | `75` | 🚦 | ✓ |
| Sem sinal de GPS | `- -` | — | — |

A presença do ícone e da barra **é** o aviso de ponto à frente: o motorista percebe que
algo apareceu na faixa inferior antes de ler qualquer dígito. Em Zona Segura a faixa
fica vazia de propósito — o vazio é a mensagem, e é ele que dá contraste ao alerta.

#### Inquilinato das faixas — um ocupante por vez

**Faixa inferior (44 px)** — "há alerta, ou por que não há":

| Prioridade | Ocupante | Duração |
| :---: | :--- | :--- |
| 1 | `PARE O VEÍCULO PARA ATUALIZAR` | transitório, 2 s |
| 2 | `⚠ BASE INDISPONÍVEL — sem alertas` | **persistente** |
| 3 | `⚠ SEM SINAL — alertas suspensos   0:14` | **persistente** |
| 4 | Ícone(s) + barra de proximidade | enquanto houver ponto em alcance |
| 5 | Vazia | Zona Segura |

**Faixa superior (26 px)** — status do aparelho:

| Prioridade | Ocupante | Duração |
| :---: | :--- | :--- |
| 1 | Taxa de GPS reduzida (RF07) | persistente |
| 2 | Barra de ajuste de brilho | transitório, ~1,5 s |
| 3 | Relógio `dd/mm/aa hh:mm` | padrão |

O relógio vem do GPS. **Não há RTC com bateria no BOM**, então ele mostra
`--/--/-- --:--` até o primeiro fix — até **26 s** em cold start (RNF datasheet). Fuso
fixo em **UTC−3**, sem lógica de horário de verão: o Brasil o extinguiu em 2019.

O tempo decorrido em "sem sinal" não é enfeite: `0:14` é um viaduto e `3:20` é problema
real, e a ação do motorista difere nos dois casos.

#### Paleta

Três regras, todas consequência do **brilho ser controlado por PWM do backlight**, que
multiplica a luminância de tudo na tela pelo mesmo fator:

1. **Distinguir por matiz, nunca por luminância.** Diferenças de valor são justamente o
   que o PWM destrói: a 5% de brilho, um vermelho escuro desaparece e um vermelho claro
   sobrevive. Todas as cores de estado ficam em valor alto e diferem em *hue*.
2. **Nunca por saturação apenas.** Um "rosa claro" `#FF8080` tem **matiz 0 — o mesmo do
   vermelho**, diferindo só em saturação, que é a primeira coisa a colapsar no escuro.
   O rosa precisa de azul de verdade.
3. **O número fica sempre branco.** Ele é o que precisa ser lido; branco dá 21:1 de
   contraste sobre preto, contra 5,3:1 do vermelho — quatro vezes mais. A cor do risco
   vai para a barra, que não é texto e não perde nada ao ser colorida.

| Uso | Hex | RGB565 | Matiz |
| :--- | :--- | :--- | ---: |
| Fundo | `#000000` | `0x0000` | — |
| **Número, denominador, texto** | `#FFFFFF` | `0xFFFF` | — |
| Estado degradado (sem base, sem sinal, `- -`) | `#C0C0C0` | `0xC618` | neutro |
| Fio de moldura e trilho vazio da barra | `#404040` | `0x4208` | neutro |
| **Barra — aproximação conforme e semáforo** | `#FFB000` | `0xFD80` | 42° |
| **Barra — margem** | `#FF40C0` | `0xFA18` | 318° |
| **Barra — perigo** | `#FF0000` | `0xF800` | 0° |

Separação de matiz entre as cores de barra: âmbar↔vermelho 42°, vermelho↔rosa 40°,
âmbar↔rosa 84°. O mínimo de 40° é discriminável em área grande.

**Âmbar `#FFB000` em vez de amarelo puro `#FFFF00`:** o amarelo puro dessatura na direção
do branco a baixo brilho e passa a competir com o número. O âmbar mantém identidade.

**Verde não aparece na tela.** Zona Segura não tem barra, e barra só existe perto de
ponto — então as cores necessárias são três, não quatro. O verde vive no LED.

⚠️ **A definir na bancada (R-05):** se a âmbar, a rosa e a vermelha continuam
distinguíveis no piso de 5% de brilho. Se não, o piso sobe para ~10%. A cor da barra e a
do LED devem concordar, e o rosa é a mais sensível das duas calibrações.

#### Controle de brilho pelo encoder

* **Faixa de 5% a 100%**, conforme a matriz de IHM.
* **Curva perceptual, não linear.** A percepção humana de brilho é aproximadamente
  logarítmica: passos lineares de *duty cycle* fazem toda a mudança acontecer no fundo
  da escala. Use `duty = (passo / N) ^ 2,2` ou uma tabela logarítmica.
* **PWM em ≥ 20 kHz.** Abaixo de ~1 kHz o painel cintila de forma perceptível na visão
  periférica e pode produzir efeito estroboscópico com feições da estrada; entre 1 e
  20 kHz alguns módulos assobiam.
* Fundo preto permanente não é estética: com o brilho ajustado a 5% à noite, quanto menor
  a área acesa, menor o ofuscamento. Não há sensor de luz ambiente — o ajuste é manual, e
  o desenho precisa cooperar com isso.

#### Assets

Tudo em **flash** (4 MB), zero impacto no orçamento de RAM da §1 do `formato_dados.md`:

| Asset | Custo |
| :--- | ---: |
| Fonte numérica 56×94, **1 bpp** (11 glifos: `0-9` e `/`) | 7,1 KiB |
| Fonte de texto 12×20, 1 bpp, ASCII | 2,9 KiB |
| 3 sprites 40×40 RGB565 (🏎 cheio, 🏎 vazado, 🚦) | 9,4 KiB |
| **Total** | **~19 KiB** |

**1 bpp com a cor aplicada no blit** é o que faz uma única fonte servir todos os estados;
em RGB565 gastaria 16× mais e exigiria uma cópia por cor. Os sprites são **três** porque
o `TYPE=2` **compõe** 🚦 com 🏎 em vez de ser um quarto desenho.

Os "emoji" são **sprites próprios, não caracteres**: não há sistema operacional nem pilha
de fontes no Pico, e o emoji colorido do desktop vem de uma fonte de vários megabytes.
O caminho reproduzível é um script que renderize o desenho de origem uma vez e converta
para array C, versionado junto ao código.

#### Custo de redesenho por região (SPI a 32 MHz)

| Região | Bytes | Tempo |
| :--- | ---: | ---: |
| Tela inteira | 150,0 KiB | 38,4 ms |
| Faixa inferior 320×44 | 27,5 KiB | 7,0 ms |
| Barra ~240×28 | 13,1 KiB | 3,4 ms |
| Fio de moldura 2 px | 4,3 KiB | 1,1 ms |

O ciclo nominal é de 4 Hz, ou 250 ms. Como **o layout não muda entre estados** e nada
pisca, o redesenho típico é parcial: número, barra e faixas, não a tela toda.

---

## 🔄 5. Fluxo Lógico de Navegação de Telas

```text
       [ TELA DE BOOT ]
       (Lê o SD, valida radares.bin, carrega 214 KB na RAM)
              │
              ├── Falha de SD / base inválida ──> [ TELA DE FALHA DE DADOS ]
              │                                    (modo velocímetro simples)
              ▼
       [ TELA SEM SINAL ] (aguardando primeiro fix)
              │
              ▼
    ┌───> [ TELA VELOCÍMETRO (Pista Livre) ] <────────────────────────┐
    │     (Vel. atual, fundo preto, LED RGB verde fixo)               │
    │         │                                                       │
    │         │ Ponto válido ≤ 300m e à frente                        │ Afastou-se
    │         │                                                       │ (histerese 340m)
    │         ├── limite == 0 ──> [ TELA DE SEMÁFORO ]────────────────┤
    │         │                   (Amarelo/Vermelho 2Hz, SILENCIOSO) │
    │         │                                                       │
    │         └── limite > 0 ───> [ TELA DE APROXIMAÇÃO ]             │
    │                              (Distância + placa de limite)      │
    │                                  │                              │
    │                                  ├─ Vel. ≤ limite ──> LED amarelo fixo   │
    │                                  │                    (conforme)         │
    │                                  │                                       │
    │                                  ├─ Vel. ≤ V_infra ─> LED ROSA 1Hz       │
    │                                  │                    (margem, silente)  │
    │                                  │                                       │
    │                                  └─ Vel. > V_infra ─> [ TELA DE PERIGO ]─┤
    │                                                       (Fundo vermelho,   │
    │                                                        LED 4Hz, buzzer   │
    │                                                        em 3 faixas)      │
    │                                                                          │
    │   PRECEDÊNCIA (RF03.4):  Perigo > Margem > Semáforo > Conforme           │
    │                                                                 │
    │  Perda de fix > 10s ──> [ TELA SEM SINAL ] ─────────────────────┤
    │                                                                 │
    └─ Fim do Download <─── [ TELA ATUALIZAÇÃO WI-FI ] <─── Clique SW ─┘
                                                           (só se parado)
```

---

## 🔗 6. Rastreabilidade da Revisão 2

| Item de `revisao_tecnica.md` | Onde foi aplicado |
| :--- | :--- |
| R-01 — `VBUS` é o pino 40, usar `VSYS` | RNF01 |
| R-03 — Cálculo de distância sobre a base inteira | RF02.1 |
| R-04 — Wraparound e `DirType` | RF02.3 |
| R-06 — Conector P2 do buzzer | RNF05 — ✅ **confirmado: JST-XH** |
| R-07 — Configuração UBX | RF01.2 |
| R-08 — `$GNRMC` / `$GPRMC` + checksum | RF01.1 |
| R-09 — Condição de aproximação e histerese | RF03.1, RF03.2 |
| R-10 — OTA atômico e validação | RF05.2, RF06.3 |
| R-11 — Credenciais fora do firmware | RF05.3 |
| R-12 — OTA com veículo parado | RF05.1 |
| R-15 — Mutex SPI0 e divisão entre núcleos | RNF04 |
| R-16 / R-17 — Buzzer: diodo e transistor | RNF02 e `bom_schematic.md` |
| R-19 — `limite == 0` e Zona de Semáforo | RF03, RF03.3 |
| R-20 — Significado de `TYPE=5` | RF03.5 — reescrito de trecho controlado para **Radar Móvel** |
| L-01 — Perda de fix | RF07 |
| L-02 — Orçamento de memória | RNF07 |
| L-03 — Falha de cartão SD | RF07, RNF03 — refinado com `DET` em 2026-09-17 |
| L-04 — Watchdog | RF07 |
| L-05 — Faixa térmica | RNF09 |
| L-06 — Conversão de velocidade | RF01.3 |
| L-07 — Estratégia de testes | RNF10 |
| L-08 — Linguagem e runtime | RNF06 |
| L-09 — Precisão numérica | RF02.2, RNF08 |
| R-21 — 5 Hz é o teto do módulo em GPS+GLONASS | RF01, RF01.2, RF01.4 — ✅ **4 Hz nominal / 3 Hz de piso, confirmado**; monitoramento no RF01.5 |
| R-22 — GPIO de 3,3 V excede `VIN` do GPS | `bom_schematic.md` §3 — resistor de 1 kΩ em série |
| R-23 — Faixas de 10%/20% vazias na maioria dos radares | RF03.7 — faixas reancoradas em `V_infra` |
| R-24 — Margem suprimida pelo semáforo na precedência por categoria | RF03.4 — margem elevada acima do semáforo |
| R-20 — `TYPE=5` afere velocidade média | **Hipótese descartada.** RF03.5 reescrito para Radar Móvel; RF03.10 anulado |
| R-26 — `TYPE=2` é semáforo e não recebe aviso | RF03.3 — lacuna registrada; forma deferida para a sessão de telas |
| E-02 / E-03 / E-04 — Correções de texto | RNF05, RNF02, matriz IHM |

**Confirmado pelo autor (2026-09-15):** RNF05 (conector JST-XH), RF01.4 (GPS+GLONASS a
4 Hz nominal, piso de 3 Hz), RF03.4 (precedência por categoria), RF03.6 (margem de
segurança de 6 km/h e 5%), RF03.7 (buzzer em 3 faixas), RF03.8 (Aproximação silenciosa)
e RF03.9 (rosa piscante na margem).

**Removido do projeto (2026-09-15):** LED verde de status de Wi-Fi. O LED RGB é o único
indicador luminoso.

Nenhuma suposição pendente neste documento.

❌ **Anulado:** RF03.10 (velocidade média) — sem dados sobre os quais operar.
🎨 **Sessão dedicada:** desenho de telas e interfaces visuais — inclui **R-26**
(indicação combinada de semáforo + radar) e a distinção visual de Radar Móvel (RF03.5).

Itens em aberto que não afetam este documento: R-05, R-13, R-14 (ver `bom_schematic.md`).
