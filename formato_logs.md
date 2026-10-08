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

Arquivo minúsculo, reescrito a cada ponto gravado. Permite retomar uma viagem
depois de desligar e religar o aparelho, em vez de abrir outra. Guarda se há
viagem ativa, a data/hora da última amostra e a distância acumulada.

Não é log e não sobe para o servidor.

---

## O que o servidor aceita

O leitor de viagens (`servidor/viagem.py`) aceita **v1, v2 e v3**, e conta as
colunas por índice. Recusar uma versão antiga apagaria da página as viagens
que já estão no servidor.

Arquivos aceitos para envio, por nome: `coruja.log`, `infracoes.log` e
`AAAAMMDD_HHMMSS.log`. O `remessa.log` **não** está na lista, e é isso que o
mantém no cartão.
