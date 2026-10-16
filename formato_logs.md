# 📝 Formatos dos arquivos de log

Consulta rápida: que arquivo o aparelho grava no cartão, com que colunas, e
**qual versão de firmware produz cada forma**.

> Este documento trata dos arquivos que o aparelho **escreve**. O formato do
> `radares.bin`, que ele **lê**, está em [`formato_dados.md`](formato_dados.md).

Todos os carimbos de tempo são **UTC**. A conversão para o fuso local é de
quem exibe (ADR 0002).

---

## Regra de versionamento

A primeira linha de cada arquivo declara o formato. **Um rótulo nunca é
reusado para um conjunto de colunas diferente**, mesmo que a forma anterior
nunca tenha chegado a um cartão — porque o firmware que a grava pode estar
publicado e ser gravado depois.

Colunas novas entram **no fim**, e quem lê conta por índice tolerando faltar
do final. Assim um leitor da forma curta continua lendo a longa.

---

## 1. Viagem — `AAAAMMDD_HHMMSS.log`

Um arquivo por viagem, nomeado pelo instante de abertura. Uma linha por fatia
de amostragem.

### Histórico

| versão | firmware | taxa | colunas |
|---|---|---|---|
| **v1** | v0.1.0 – v0.1.1 | 1 por minuto | 5 |
| **v2** | v0.2.0 – v0.2.10 | 10 por minuto (6 s) | 5 |
| **v3** | v0.2.11 (não publicada) | 10 por minuto | 9 |
| **v3** | a partir da próxima | 10 por minuto | **12** |

⚠️ **Duas observações que evitam engano:**

- Da v1 para a v2 **as colunas não mudaram** — mudou o significado do segundo
  no carimbo. Na v1 ele era sempre `00`, porque a linha descrevia um minuto
  inteiro; na v2 ele é o início da fatia de seis segundos.
- A v3 cresceu de 9 para 12 colunas **sem virar v4**: a v0.2.11, único
  firmware que grava a forma de 9, nunca foi publicada nem gravada. Já `v2`
  **não estava livre**, apesar de nenhum cartão tê-la: cinco releases
  publicadas a gravam com 5 colunas.

### Colunas (v3, 12 colunas)

```
utc;lat;lon;v_media;dist_km;radar_m;radar_kmh;perto_m;perto_kmh;rumo;zona;n_radares
```

| coluna | desde | significado |
|---|---|---|
| `utc` | v1 | início da fatia, `AAAA-MM-DDTHH:MM:SSZ` |
| `lat`, `lon` | v1 | última leitura da fatia, 5 casas |
| `v_media` | v1 | média das leituras da fatia, km/h |
| `dist_km` | v1 | distância acumulada da viagem |
| `radar_m` | v3 | distância ao radar **alertado**, menor da fatia |
| `radar_kmh` | v3 | limite desse radar |
| `perto_m` | v3 | distância ao radar **mais próximo**, menor da fatia |
| `perto_kmh` | v3 | limite desse radar |
| `rumo` | v3 | rumo do veículo em graus do norte, da mesma leitura de `lat`/`lon` |
| `zona` | v3 | a mais grave da fatia (ver tabela abaixo) |
| `n_radares` | v3 | quantos passaram por todos os filtros, no pior instante |

**Campo vazio ≠ zero.** Vazio é "não havia" ou "não se sabe"; zero é valor
medido. Importa em três lugares:

- `radar_kmh`/`perto_kmh` — zero é limite válido: é o do semáforo;
- `rumo` — zero é norte. A RMC vem sem rumo com o veículo **parado**, e é
  justamente parado que o receptor não sabe a direção.

### Valores de `zona`

Ordem de gravidade crescente — o maior é o pior, e é por isso que a agregação
da fatia é o máximo.

| valor | zona | o que o motorista percebe |
|---|---|---|
| 0 | sem sinal | alertas suspensos |
| 1 | segura | nada |
| 2 | aproximação conforme | amarelo fixo |
| 3 | semáforo | — |
| 4 | aproximação com margem | rosa, 1 Hz |
| 5 | **perigo** | vermelho 4 Hz **e buzzer** |

### Por que `radar_m` e `perto_m` são dois campos

O radar alertado vence por **gravidade** (RF03.4), não por distância. No Eixão
a pista lateral corre a poucos metros da principal com limite menor, e o radar
dela ganha sempre de quem está a 80 na principal. **A divergência entre as
duas colunas é o diagnóstico**; com uma só, o problema fica invisível.

---

## 2. Infrações — `infracoes.log`

Acumulativo, uma linha por infração registrada. **Formato `v1` desde a
v0.1.0** — nunca mudou.

```
utc;lat;lon;rumo;v_radar;v_max;v_infra;limite;radar_lat;radar_lon;dist_min
```

| coluna | significado |
|---|---|
| `utc` | instante da passagem |
| `lat`, `lon` | posição do veículo |
| `rumo` | rumo do veículo, graus |
| `v_radar` | velocidade no instante da menor distância |
| `v_max` | maior velocidade durante a aproximação |
| `v_infra` | limiar de multa do radar (limite + 6 até 100 km/h; +5% acima) |
| `limite` | limite do radar |
| `radar_lat`, `radar_lon` | posição do radar |
| `dist_min` | menor distância alcançada, metros |

---

## 3. Diário do aparelho — `coruja.log`

Acumulativo, texto livre. **Não tem cabeçalho de versão** — é log, não dado
estruturado. A forma da linha mudou duas vezes:

| firmware | forma |
|---|---|
| v0.1.0 – v0.2.1 | `[NÍVEL] origem: mensagem` |
| v0.2.2 – v0.2.9 | `IDEXEC [NÍVEL] origem: mensagem` |
| **v0.2.10 em diante** | `CARIMBO IDEXEC [NÍVEL] origem: mensagem` |

```
2026-10-07T14:41:22Z EGMHMD4E [INFO] http: PUT https://.../envio/coruja.log
+00003412 EGMHMD4E [INFO] sd: driver do cartao iniciado
```

- **`CARIMBO`** é ISO 8601 quando o relógio foi acertado, ou `+milissegundos
  desde o boot` quando não foi. As duas formas não se confundem nem de olho
  nem por expressão regular: o `+` as separa. O RP2350 não tem bateria no
  relógio, então o começo de toda ligação é genuinamente sem hora — inventar
  1970 ali seria pior que admitir.
- **`IDEXEC`** são 8 caracteres sorteados no boot, do alfabeto
  `23456789ABCDEFGHJKLMNPQRSTUVWXYZ` (sem `0`, `1`, `I`, `O`, `l`, que são
  onde a leitura erra). Agrupa as linhas de uma mesma ligação num arquivo que
  acumula entre elas.
- **`NÍVEL`** é `DEBUG`, `INFO`, `WARN` ou `ERRO`.

### Linhas que existem para diagnóstico

Até a **v0.2.12** este arquivo cobria só o boot: nenhuma das viagens gravadas
no cartão aparecia nele, nem a abertura do arquivo. Quando uma viagem de
08/10/2026 registrou "segura" do começo ao fim passando a 43 m de um radar,
não havia como saber qual binário estava rodando, se o piloto tinha base, nem
o que ele via. Da **v0.2.13** em diante:

| origem | linha | o que responde |
|---|---|---|
| `boot` | `v0.2.13` | qual firmware escreveu este log |
| `base` | `piloto recebeu N pontos` | a base chegou a quem alerta — sai como **ERRO** se `N` for zero |
| `vigia` | `base N pts \| fix sim/NAO \| zona Z \| hdop H (S sat)` | a cada 5 min, o estado de quem decide o alerta |
| `temp` | `47.3` | pastilha do RP2350, a cada 5 min |
| `gps` | `primeiro fix em 168.4 s (9 sat, hdop 1.2)` | quanto a partida demorou, uma vez por ligação |
| `gps` | `radio: ruido 85 \| agc 1800/8191 \| antena ok \| interferencia 42/255 (limpo) \| ttff do modulo 166.1 s (ligado ha 172.8 s) \| aop ligado` | por que ela demorou — no primeiro fix e a cada 5 min |
| `gps` | `radio: MON-HW sem resposta \| ttff do modulo 38.2 s (ligado ha 44.9 s) \| aop ligado` | a mesma linha quando uma das três perguntas não voltou |
| `diario` | `trecho fechado: veiculo parado` | fechou o arquivo; **a gravação continua** |
| `diario` | `viagem retomada` / `viagem nova: a parada passou de 2 h` | o que a energização decidiu sobre a viagem anterior |
| `gps` | `taxa de fixes em 2.0 Hz com posicao presente (falha)` | **WARN** — a cadência caiu com o receptor entregando posição |
| `gps` | `taxa de fixes normalizada: 4.0 Hz` | INFO — fecha o evento acima |
| `gps` | `taxa em falha e receptor calado: reenviando a configuracao (RF01.5)` | **WARN** — a tentativa de recuperação em voo começou |
| `gps` | `reconfiguracao em voo: configurado; a porta ficou em 115200` | como ela terminou (ERRO se não foi `configurado`) |

⚠️ **A linha `radio:` compara dois relógios de propósito.** O `primeiro fix`
conta desde o boot do Pico e inclui a configuração do receptor; o `ttff do
modulo` é o que o próprio módulo mediu, e o `ligado ha` é o relógio interno
dele. Com os três, a diferença entre os dois cronômetros vira aritmética em
vez de inferência — na partida de 15/10/2026 ela foi de 7,2 s. Divergirem
muito aponta o atraso para o nosso lado, e não para o céu. Ela sai como
**WARN** quando a interferência derruba o fix ou a antena está em curto ou
aberta — que é quando o aparelho parece quebrado sem estar.

🔴 **Cada uma das três perguntas diz se respondeu.** Em 15/10/2026 o diário
gravou `ruido 0 | agc 0/8191 | antena iniciando | interferencia 0/255` numa
partida fria: o `MON-HW` não respondeu dentro dos três segundos de teto — a
linha saiu exatamente 3000 ms depois do fix, esgotando a espera — e o que
foi impresso era a estrutura zerada. `agc 0/8191` lê-se como front-end
morto, e não havia nada de errado com o front-end. Agora a parte que não
voltou aparece como `MON-HW sem resposta`, `NAV-STATUS sem resposta` ou
`NAV-AOPSTATUS sem resposta`, e o alarme de antena/interferência só julga o
que foi de fato medido.

⚠️ **A degradação de taxa só existe aqui.** Ela ocupava a faixa superior da tela de
dirigir e saiu dela em 14/10/2026: como a taxa conta apenas fixes aproveitados, o aviso
aparecia em todo túnel, no lugar do relógio, dizendo o que o `SEM SINAL` já dizia. A
linha do log sai sob a condição que a torna informativa — taxa baixa **com fix
presente**, sustentada por mais de 3 s. Um receptor que voltou a 1 Hz por ter perdido a
configuração não aparece em mais lugar nenhum: as barras de HDOP medem geometria, não
cadência, e continuam cheias.

⚠️ **A `vigia` amostra de cinco em cinco minutos.** Ela prova base e fix; a
`zona` ali é um instante qualquer, e quase nunca cai num alerta. Quem responde
"o aparelho viu o radar?" é o log de **viagem**, que tem uma linha a cada 6 s.

### `remessa.log`

Mesma forma do `coruja.log`. Existe desde a **v0.2.8**, e recebe o log
**durante o envio de dados**, quando ele é desviado para cá.

O desvio é necessário porque o `coruja.log` é um dos arquivos que sobem: cada
linha gravada durante o próprio envio o faria crescer, e arquivo que cresceu
não é apagado — ele subiria a cada remessa e nunca sairia do cartão.

⚠️ **O `remessa.log` não sobe e não é apagado**, de propósito: é o único
registro que sobrevive a um envio que falhou. Antes dele, o envio era cego —
três falhas em campo não deixaram uma linha no cartão nem uma requisição no
servidor.

---

## 4. Estado da viagem — `viagem.est`

Arquivo minúsculo, reescrito a cada ponto gravado. Guarda se a **gravação
está ligada**, a data/hora da última amostra e a distância acumulada.

Não é log e não sobe para o servidor.

### Gravar é um MODO, e só o menu o desliga

Desde 15/10/2026 o item de menu liga e desliga um modo, em vez de abrir e
fechar uma viagem. Dentro dele as viagens começam e terminam sozinhas:

| o que acontece | o arquivo | a quilometragem |
|---|---|---|
| clicou **iniciar** | abre no primeiro fix | zera |
| 5 min parado | **fecha** | continua |
| voltou a andar em menos de 2 h | abre outro | **continua** |
| voltou a andar depois de 2 h | abre outro | **zera** — viagem nova |
| corte de energia, religou em menos de 2 h | abre outro | continua |
| corte de energia, religou depois de 2 h | abre outro | zera — viagem nova |
| clicou **parar** | fecha | zera, e o modo desliga |

Duas regras valem a pena destacar:

- **Nenhuma regra automática desliga a gravação.** Antes, cinco minutos
  parado encerravam tudo e só o menu religava — bastava um semáforo longo
  para o resto do trajeto não ser gravado, sem ninguém avisar.
- **A janela de 2 h é a mesma dos dois lados** — com o aparelho ligado e
  depois do corte de energia —, porque é a mesma pergunta: "isto ainda é a
  mesma viagem?". O limite é **inclusivo**: 120 minutos cravados ainda são a
  mesma viagem. O caso que define o número é o almoço de estrada.

O que o servidor vê disso: trechos cuja `dist_km` emenda viram **uma jornada
só**, com um pino de pausa entre eles; quando ela zera, começa outra jornada.

### Limite conhecido

Com o aparelho **ligado e com fix**, parar o carro por duas horas não começa
viagem nova — ele apenas fecha o trecho em cinco minutos e espera o
movimento. A contagem das 2 h sai do carimbo do último ponto gravado, então
ela mede o tempo entre uma amostra e a seguinte. Na prática a alimentação é
pós-chave e o aparelho desliga junto com o carro, que é o caso para o qual a
regra foi escrita.

---

## 5. 🔴 `gps.est` — existiu por um dia, e não funcionava

Entre 15 e 16/10/2026 o aparelho guardava aqui em que baud o módulo havia
respondido, para começar a sondagem por ele e poupar os ~1,7 s que ela custa.

**Quatro partidas de verdade mostraram que a premissa estava errada.** O
padrão não é "igual à última vez":

| partida | baud do módulo |
|---|---|
| 1ª do dia (15/10) | 9600 |
| logo em seguida | 115200 |
| 1ª do dia (16/10) | 9600 |
| logo em seguida | 115200 |

É **"9600 depois de muitas horas desligado"** — a BBR do receptor drena de um
dia para o outro e ele volta à configuração de fábrica; numa parada curta ela
sobrevive e ele lembra os 115200. Lembrar a última resposta prevê o contrário
em toda transição, e nessa amostra toda partida é uma transição: erraria 4 de
4, enquanto começar sempre em 9600 acerta 2 de 4.

O arquivo foi removido. O conserto não foi adivinhar melhor, e sim **perguntar
barato**: uma tentativa curta em cada baud (`kEsperasDaSondagem`), e só depois,
se nenhum responder, a insistência inteira. Ver `ConfiguradorGps`.

Um cartão que rodou a v0.2.15 pode ter um `gps.est` parado nele. É inofensivo
— nada mais o lê — e pode ser apagado à mão.

---

## O que o servidor aceita

O leitor de viagens (`servidor/viagem.py`) aceita **v1, v2 e v3**, e conta as
colunas por índice. Recusar uma versão antiga apagaria da página as viagens
que já estão no servidor.

Arquivos aceitos para envio, por nome: `coruja.log`, `infracoes.log` e
`AAAAMMDD_HHMMSS.log`. O `remessa.log` **não** está na lista, e é isso que o
mantém no cartão.
