# 🔬 Primeira integração no host — o que a prévia revelou

**Projeto:** Detector de Radares GPS Inteligente (Raspberry Pi Pico 2 W)
**Ferramenta:** `firmware/ferramentas/previa_produto`
**Data:** 2026-10-05
**Versão exercitada:** `v0.1.0` + correção do rumo

---

## 0. Por que esta prévia existiu

Os dois arquivos de log — `infracoes.log` e o de viagem — **nunca haviam sido
produzidos pelo caminho de código real**. Tudo que existia eram testes
unitários com dublês em memória.

A versão anterior da `previa_produto` fiava `PilotoAlerta` e `TelaPrincipal`
diretamente, sem passar pela `Aplicacao`. Servia para ver alerta e tela, mas
deixava de fora tudo que vive na `Aplicacao`: o menu, o `DetectorParado`, a
gravação de ajustes e, desde 2026-10-04, o `DiarioBordo`.

**A primeira integração de verdade seria no carro**, que é o lugar mais caro
possível para achar um defeito de formato. Esta prévia antecipou isso para a
mesa, e achou um.

---

## 1. O que foi exercitado

| Peça | De onde veio |
| :--- | :--- |
| `Aplicacao`, `DiarioBordo`, `MaquinaZona`, `MenuAjustes`, telas, `PilotoAlerta` | **o mesmo objeto que vai para o RP2350** (ADR 0001) |
| GPS | simulador percorrendo o Eixão, rota do OpenStreetMap, a 4 Hz |
| Base | `radares.bin` real, **18.322 pontos**, formato v2 |
| Cartão | diretório do sistema de arquivos (`ArmazenamentoArquivo`) |
| Encoder | teclado em modo cru |
| LED e buzzer | texto no rodapé |

Velocidade fixa em **100 km/h**, acima do `V_infra` da maioria dos radares do
Eixão — escolhida para provocar infrações em vez de esperá-las.

---

## 2. O que funcionou

### 2.1 A integração de distância acerta na casa do centímetro

```
12:13:00  0.71
12:14:00  2.38    Δ 1.67
12:15:00  4.04    Δ 1.66
12:16:00  5.71    Δ 1.67
12:17:00  7.38    Δ 1.67
12:18:00  9.04    Δ 1.66
```

A 100 km/h, um minuto são **1,6667 km**. Os desvios de ±0,01 são o
arredondamento de duas casas do próprio formato.

**Isso valida a decisão de integrar velocidade Doppler em vez de somar
distâncias entre coordenadas** (`AcumuladorViagem`). A alternativa teria
acumulado erro a cada ponto; esta não acumula nenhum em seis minutos.

### 2.2 O detector de infração dispara, não duplica, e acerta o radar

Seis passagens em seis minutos, **uma linha cada**, nenhuma repetida:

```
utc;lat;lon;rumo;v_radar;v_max;v_infra;limite;radar_lat;radar_lon;dist_min
12:15:52;-15.82806;-47.91548;38;100.0;100.0;66.0;60;-15.82824;-47.91513;42.9
12:16:03;-15.82588;-47.91357;58;100.0;100.0;86.0;80;-15.82618;-47.91337;39.6
12:17:18;-15.81479;-47.89811;47;100.0;100.0;86.0;80;-15.81490;-47.89798;18.4
12:18:10;-15.80538;-47.88876;35;100.0;100.0;86.0;80;-15.80540;-47.88866;11.3
12:18:52;-15.79637;-47.88354;20;100.0;100.0;66.0;60;-15.79631;-47.88352; 6.9
12:19:46;-15.78309;-47.88057; 0;100.0;100.0;86.0;80;-15.78304;-47.88047;12.0
```

As duas primeiras linhas são o cenário que motivou expor o **candidato mais
próximo** no `Veredito`: dois radares a **11 segundos** um do outro, com
limites diferentes (60 e 80). Cada passagem foi atribuída ao seu radar.

`v_radar` igual a `v_max` em todas é esperado — a velocidade é constante na
simulação. Num trajeto real as duas devem divergir, e a diferença é o que
mostra se o alerta fez reduzir.

### 2.3 A retomada depois do corte de energia

**É o teste que no carro custaria uma parada no posto.** Feito aqui matando o
processo:

| | |
| :--- | :--- |
| Estado antes | `ativa=1 · utc=2026-10-05T12:18Z · dist_km=9.04` |
| Processo morto, religado **sem** `--viagem` | |
| Arquivo novo | `20261005_121912.log` |
| Distância ao religar | **9,59 km** — continuou de 9,04 |
| Primeiro ponto do trecho novo | `12:19:00 … 10.37` |

O delta do primeiro ponto do segundo trecho é **1,33 km**, e de 12:19:12
(retomada) até 12:20:00 são 48 s = 1,33 km. Fecha.

A janela de 120 minutos, a continuidade da distância e o arquivo novo por
trecho funcionam como projetados.

---

## 3. 🔴 O defeito encontrado: `rumo 360`

A última passagem saiu assim:

```
2026-10-05T12:19:46Z;-15.78309;-47.88057;360;100.0;…
                                          ▲
```

**360 não é rumo.** A faixa é 0 a 359, e o valor fica a um dígito do
sentinela `999`, que marca rumo inválido — ou seja, o arquivo ficaria
ambíguo justamente no campo que existe para não ser ambíguo.

**A causa.** O formatador arredondava com

```cpp
static_cast<unsigned>(m.rumo_graus + 0.5F)
```

e qualquer rumo de 359,5 a 359,99 vira 360,0 a 360,49, que o corte para
inteiro deixa em **360**.

**Por que os testes unitários não pegaram.** O único caso de arredondamento
que eu havia escrito era `256,7 → 257`. Nunca a volta do círculo. O Eixão
corre quase norte-sul, então a prévia atravessou o zero — coisa que uma
coordenada de teste escolhida à mão não faz.

**A correção** é `% 360U`, com teste cobrindo 359,5 / 359,7 / 359,99 e as
bordas 0 / 0,4 / 0,5 / 180 / 359 / 359,4, mais mutante que mata a remoção do
fecho.

---

## 4. ⚠️ Calibração: o raio de 50 m tem margem fina

O `dist_min` existe no arquivo justamente para calibrar o `kRaioPassagemM`,
que eu escolhi por estimativa. Os valores medidos:

```
42.9   39.6   18.4   12.0   11.3   6.9
```

**Dois dos seis ficaram a menos de 11 m do corte de 50 m.** Um radar a 55 m
da trajetória seria descartado como "passou perto, não passou por ele".

⚠️ **Mas este número ainda não vale para decidir.** O simulador percorre a
**linha de centro** do Eixão, e o veículo real anda numa faixa — o
afastamento lateral até o radar será outro. Além disso, parte dos 42,9 m pode
ser imprecisão da própria base, não da trajetória.

**O que fazer:** repetir a leitura depois da primeira volta real e comparar.
Se os `dist_min` reais também encostarem em 50, o raio sobe. A coluna
`radar_lat`/`radar_lon` ao lado da posição do veículo mede, de quebra, a
precisão da base.

---

## 5. Um artefato da prévia que **não** é do aparelho

O primeiro ponto da primeira corrida deu **0,71 km** onde o tempo decorrido
pedia 0,94. Os minutos cheios fechavam perfeitos, então era coisa do arranque.

**Experimento discriminante.** Deixei o simulador rodando **15 s** antes de
abrir a prévia:

| | Corrida 1 (~8 s de atraso) | Corrida 2 (15 s de atraso) |
| :--- | ---: | ---: |
| Prévia aberta às | — | **12:22:26** |
| Arquivo nomeado | `121326` | **`122211`** — 15 s antes |
| Primeiro ponto | 0,71 km | **0,94 km** |
| Tempo real de leitura até a virada | ~26 s | **34 s** |
| Distância esperada | 0,72 km | **0,94 km** ✅ |

**Mecanismo confirmado.** A pty acumula o NMEA enquanto ninguém lê. Ao abrir,
a prévia consome a fila num rajada: o **nome do arquivo** vem do carimbo da
primeira sentença, que está velho, e a **distância** integra o relógio
monotônico, que só anda em tempo real. Os dois estão corretos — medem coisas
diferentes.

### Por que o aparelho não faz isso

O UART do RP2350 tem **FIFO de 32 bytes**, e uma sentença RMC tem cerca de
70. Com o laço bloqueado, o aparelho **descarta** sentenças em vez de
enfileirá-las, e volta a ler com horário atual.

> 📌 **Registrado para que ninguém "conserte" no aparelho um problema que ele
> não tem.** O artefato é da borda da prévia, não do produto.

O efeito real no aparelho é outro e menor: com sentenças descartadas, o fix
seguinte chega com `Δt` acima do teto de `kMaxPassoIntegracaoMs` (2 s) e
aquele intervalo não é integrado — a distância encurta pelo tempo bloqueado.
E a única operação que bloqueia o laço por dezenas de segundos é o **OTA**,
que só roda com o veículo parado (RF05.1), onde não há distância a perder.

---

## 6. O que a prévia ainda **não** exercita

Lista honesta, para não confundir "passou na prévia" com "testado":

| Não exercitado | Por quê |
| :--- | :--- |
| **Navegação do menu até `viagem`** | o `--viagem` pula o menu, porque a prévia roda em movimento e o menu exige o `DetectorParado`. A navegação tem oito testes próprios. |
| **Encerramento automático por 5 min parado** | o simulador não para sozinho |
| **A borda dos 120 min** da janela de retomada | exige esperar duas horas, ou mexer no relógio |
| **OTA** | fala com Wi-Fi; a `AcoesTexto` só conta as chamadas |
| **Audibilidade do buzzer** (R-32) | é julgamento subjetivo, e o buzzer aqui é texto |
| **Qualquer coisa de hardware** | antena, LDO, ESD, temperatura |

---

## 7. Como repetir

```sh
# num terminal
simulador/simula_gps.py --velocidade 100 --sem-pausa

# noutro
firmware/build-host/ferramentas/previa_produto --viagem --cartao /tmp/coruja_previa
```

Sem `--viagem`, a viagem só começa pelo menu (`a`/`d` giram, espaço clica) ou
por retomada automática a partir de um `viagem.est` dentro da janela.

Os arquivos saem no diretório de `--cartao`, com os mesmos nomes e o mesmo
formato que sairiam no microSD.

---

## 8. Saldo

| | |
| :--- | :--- |
| ✅ Validado | integração de distância, detecção de infração, atribuição ao radar certo, retomada com continuidade de distância, formato das duas linhas |
| 🔴 Corrigido | `rumo 360` no `infracoes.log` |
| ⚠️ A calibrar | `kRaioPassagemM`, depois da primeira volta real |
| 📌 Registrado | o artefato de acúmulo na serial, que é da prévia e não do aparelho |

**Um defeito achado na mesa custa minutos; o mesmo defeito achado dirigindo
custa uma volta de carro inteira** — e seria descoberto depois, lendo um
arquivo que não se sabe se está certo.
