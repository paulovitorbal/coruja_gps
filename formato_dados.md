# 💾 Formato de Dados e Estratégia de Carregamento

**Projeto:** Detector de Radares GPS Inteligente (Raspberry Pi Pico 2 W)
**Resolve:** R-02, R-03 e R-18 de `revisao_tecnica.md`
**Base de referência:** 18.294 pontos no padrão iGO8, analisada em 2026-09-15
**Conversor:** `converte.py`
**Data:** 2026-09-15

---

## 0. Formato de entrada — CONFIRMADO contra o arquivo real

O arquivo de origem traz cabeçalho explícito, o que elimina a principal incerteza do
projeto:

```
X,Y,TYPE,SPEED,DirType,Direction
-44.021044,-19.799916,1,30,1,257
```

| Coluna | Campo | Faixa observada | Nota |
| :---: | :--- | :--- | :--- |
| 0 | `X` | −67,958472 … −34,815034 | **longitude — vem PRIMEIRO** |
| 1 | `Y` | −32,676090 … +3,072823 | latitude |
| 2 | `TYPE` | 1, 2, 3, 5 | 4 valores distintos |
| 3 | `SPEED` | 0, 30…120 | km/h; **0 em 1.430 registros** |
| 4 | `DirType` | 0, 1, 2 | semântica do sentido |
| 5 | `Direction` | 0…359 | rumo de captura em graus |

> ⚠️ **A ordem `X,Y` com longitude primeiro é o bug clássico deste formato.** Trocar os
> dois não gera erro algum: as coordenadas caem no oceano Índico e o sistema
> simplesmente nunca detecta nada.

### 0.1 Semântica do `DirType` — confirmada por análise geométrica

A documentação do formato não estava disponível, então a semântica foi **inferida dos
próprios dados** e validada com um teste geométrico: para cada registro, verificou-se
se existe outro ponto a menos de 120 m com rumo aproximadamente oposto (> 150° de
diferença). Um radar unidirecional precisa de um "gêmeo" para cobrir a pista contrária;
um omnidirecional ou bidirecional, não.

| `DirType` | n | % | Com gêmeo oposto a < 120 m | Conclusão |
| :---: | ---: | ---: | ---: | :--- |
| 0 | 1.419 | 7,8% | **0,3%** | **omnidirecional** — ignora o rumo |
| 1 | 15.218 | 83,2% | **47,9%** | **unidirecional** — só o rumo indicado |
| 2 | 1.657 | 9,1% | **0,2%** | **bidirecional** — o rumo indicado e o oposto |

O contraste de **~200×** entre `DirType=1` e os outros dois é conclusivo. Reforço em
dados brutos: entre registros `DirType=1` a menos de 120 m um do outro, há **2.185**
pares com rumos opostos contra apenas **203** com rumos iguais — proporção de 10,8:1,
exatamente o que se espera se cada sentido da via é um registro separado.

> **Correção de uma suposição anterior:** eu havia previsto que `Direction = 0` seria o
> sentinela de "omnidirecional". **Está errado.** O `Direction` vem preenchido mesmo
> quando `DirType = 0` (99,6% não-zero) — é o rumo de captura registrado, informativo
> mas irrelevante para o filtro nesse caso. O sentinela é o próprio `DirType`, e apenas
> 77 registros em toda a base têm `Direction = 0` legítimo.

### 0.2 Campo `TYPE` — significado confirmado

A base de referência usa quatro valores de `TYPE`. O significado de cada um foi
**confirmado contra a fonte da base**, comparando contagens por categoria com a
distribuição no arquivo — batem na unidade, o que torna o mapeamento inequívoco.

| `TYPE` | Significado | n | `SPEED=0` | Limites observados |
| :---: | :--- | ---: | ---: | :--- |
| **1** | Radar fixo de velocidade | 11.783 | 0% | 30–120 |
| **2** | Semáforo **com** radar de velocidade | 2.994 | 0% | 30–80 |
| **3** | Semáforo com câmera (avanço de sinal) | 1.452 | **98,5%** | 0 (e 50/60 em 22 casos) |
| **5** | Radar móvel | 2.065 | 0% | 60–120 |

> ⚠️ **Não deduza o significado pelo perfil de velocidade.** Eu tentei: rotulei
> `TYPE=2` como "provável lombada eletrônica" e `TYPE=5` como "provável trecho
> controlado", a partir das faixas de limite e da distribuição espacial. **Ambos
> estavam errados.** Geometria e estatística não recuperam semântica — só a fonte
> da base recupera.

#### O mapeamento explica os dados

Com os significados corretos, o que parecia anomalia fica coerente:

* **`TYPE=3` tem `SPEED=0` em 98,5%** porque é câmera de avanço de sinal e **não
  afere velocidade** — não há limite a registrar. Os 22 casos com 50/60 são
  inconsistências da base, não equipamento combinado.
* **`TYPE=2` sempre tem limite** porque afere as duas coisas: avanço de sinal **e**
  velocidade. Daí a faixa urbana de 30–80 km/h.
* **`TYPE=5` opera em 60–120 km/h** porque pontos de fiscalização móvel ficam em
  rodovia. Os pares a ~26 m em sentidos opostos, que eu havia lido como "gêmeos de
  faixa de pórtico", são pontos de operação registrados por sentido de tráfego — a
  mesma convenção dos radares fixos.

> Se a sua base tiver outros códigos de `TYPE`, ajuste `mapeia_tipo()` no
> `converte.py` e as consequências de comportamento da §7.

### 0.3 Qualidade dos dados### 0.3 Qualidade dos dados

- **1 único** par de coordenadas exatamente duplicadas em 18.294 registros. Não há
  necessidade de deduplicação.
- Zero linhas rejeitadas pela validação de domínio do conversor (faixa de coordenadas
  do Brasil, `DirType` e `TYPE` conhecidos, `SPEED` e `Direction` em faixa).
- Caixa envolvente real dos dados: lat −32,68…+3,07 / lon −67,96…−34,82 — **mais
  estreita** que a caixa política do Brasil. Ver nota no Anexo A.

---

## 1. Decisão: carga integral em RAM, sem particionamento

```
18.294 registros × 12 bytes = 219.528 B = 214,4 KB   de 520 KB de SRAM
```

### Orçamento de memória

| Consumidor | Tamanho | Nota |
| :--- | ---: | :--- |
| Base de radares | **214,4 KB** | ✅ medido sobre o arquivo real |
| Framebuffer 240×240×16bpp | 115 KB | **maior consumidor isolado** — ver §6 |
| Pilha lwIP + driver CYW43 | ~48 KB | ⚠️ estimativa; medir |
| Stacks dos dois cores | ~8 KB | |
| Buffers de SD (setor + cache FAT) | ~4 KB | |
| `.data` / `.bss` / runtime | ~20 KB | ⚠️ estimativa |
| **Total** | **~409 KB** | **~111 KB livres de 520 KB** |

Cabe. Duas alavancas de alívio, se apertar:

1. **Renderização em bandas** em vez de framebuffer cheio → libera ~96 KB (§6).
2. **Empacotamento em 8 bytes** por registro → libera ~71 KB (§3.3).

Com as duas, o total cai a ~242 KB — folga acima de 50%.

### Por que não particionar

| | Carga integral | Quadrantes + duplo buffer |
| :--- | :--- | :--- |
| Acesso a SD dirigindo | **nenhum** | a cada cruzamento de fronteira |
| Falha de SD em movimento | irrelevante (já está em RAM) | perde a base |
| Código | um array ordenado | índice + margem + prefetch + histerese + máquina de estados |
| Problema de canto/fronteira | não existe | precisa de margem sobreposta |
| Thrashing em estrada sobre a divisa | não existe | precisa de histerese |

Cada mecanismo da coluna direita é uma fonte de bug que não precisa ser escrita,
depurada nem testada. O desenho de particionamento está preservado no **Anexo A** como
plano B, para o caso de a base passar de ~35.000 registros.

---

## 2. Layout do arquivo `radares.bin`

```
┌────────────────────────────────────────────────────────────┐
│ CABEÇALHO — 16 bytes                                       │
├────────┬────────────────┬──────┬──────────────────────────┤
│ offset │ campo          │ tipo │ valor                     │
├────────┼────────────────┼──────┼──────────────────────────┤
│   0    │ magic          │ 4 B  │ "RDR1" (0x52 44 52 31)   │
│   4    │ versao         │ u16  │ 1                         │
│   6    │ exp_escala     │ u8   │ 5  → graus × 10^5         │
│   7    │ tam_registro   │ u8   │ 12                        │
│   8    │ n_pontos       │ u32  │ 18294                     │
│  12    │ crc32          │ u32  │ CRC-32 do bloco de dados  │
├────────┴────────────────┴──────┴──────────────────────────┤
│ REGISTROS — n_pontos × 12 bytes                            │
│ ORDENADOS POR LATITUDE CRESCENTE (invariante obrigatória)  │
└────────────────────────────────────────────────────────────┘
```

Tamanho real gerado: **219.544 B = 214,4 KB**, contra 612.348 B do `.txt` de origem —
compressão de **2,8×**, o que também encurta o download OTA.

### Validação de integridade (resolve parte do R-10)

```c
bool valida(const uint8_t* buf, size_t tam_arquivo) {
    const Cabecalho* h = (const Cabecalho*)buf;
    if (memcmp(h->magic, "RDR1", 4) != 0)                     return false;
    if (h->versao != 1 || h->exp_escala != 5)                 return false;
    if (h->tam_registro != 12)                                return false;
    if (h->n_pontos == 0 || h->n_pontos > 40000)              return false;  // teto de RAM
    if (16 + h->n_pontos * 12u != tam_arquivo)                return false;
    if (crc32(buf + 16, h->n_pontos * 12u) != h->crc32)       return false;
    return ordenado_por_latitude(buf + 16, h->n_pontos);
}
```

As três últimas verificações são as que pegam download truncado, corrupção de SD e
conversor com bug. **O teste de ordenação não é opcional:** a busca binária da §4
retorna resultados silenciosamente errados se a invariante for violada — falha sem
sintoma, o pior tipo.

O teto de 40.000 em `n_pontos` é proteção de memória: acima disso a carga integral
estoura a RAM e o firmware deve recusar o arquivo em vez de travar.

**Verificação executada** sobre o `radares.bin` gerado:

```
[OK] magic          [OK] tamanho do arquivo     [OK] crc32
[OK] ordenado por latitude                      [OK] padding zerado
[OK] rumo em 0-179  [OK] round-trip de coordenadas idêntico ao .txt
```

---

## 3. Formato do registro

### 3.1 Recomendado — 12 bytes

| Off. | Campo | Tipo | Codificação |
| :---: | :--- | :--- | :--- |
| 0 | `lat` | `int32` | graus × 10⁵ — ex.: `-2233757` = −22,33757° |
| 4 | `lon` | `int32` | graus × 10⁵ |
| 8 | `limite` | `uint8` | km/h; **0 = não aplicável** (semáforo — ver §7.1) |
| 9 | `rumo` | `uint8` | `Direction / 2`, faixa 0–179 |
| 10 | `flags` | `uint8` | bits 0–1 = `DirType` · bits 2–4 = `TYPE` · bits 5–7 = reservado |
| 11 | `_pad` | `uint8` | zero |

`TYPE` assume os valores 1, 2, 3 e 5, que cabem nos 3 bits sem tabela de mapeamento —
armazena-se o valor bruto. `DirType` (0–2) cabe em 2 bits. Registro de 12 B com `int32`
nos offsets 0 e 4 mantém tudo alinhado em 4 bytes.

**Resolução:** 10⁻⁵ grau ≈ **1,1 m** em latitude. O NEO-M8N tem CEP de ~2,5 m
(⚠️ típico de datasheet), então o formato é mais preciso que a fonte.

**Rumo em passos de 2°:** o filtro do RF02 é de ±30° (ver R-04), logo a quantização não
altera nenhuma decisão. Verificado na amostra: `Direction=73` → armazenado 36 → lido 72°.

### 3.2 Decisão revisada: `DirType` como campo, não expansão de registros

Eu havia recomendado **expandir cada radar bidirecional em dois registros** com rumos
opostos, para manter o filtro como uma única comparação. Com os dados reais em mãos,
**essa recomendação muda**: são 1.657 bidirecionais, cuja expansão levaria a base a
19.951 registros = **233,8 KB**, contra 214,4 KB guardando o `DirType` em 2 bits.

Economia de **19,4 KB**, e o custo é nulo, porque a comparação bidirecional é uma dobra
aritmética em vez de um caso especial:

```c
#define TOL_RUMO_GRAUS   30      // RF02
#define VEL_MIN_RUMO     5.0     // km/h — abaixo disso o rumo do GPS é ruído (R-04)

enum { DIR_OMNI = 0, DIR_UNI = 1, DIR_BI = 2 };

// diferença circular em 0-180 — trata o wraparound 359/0 (R-04)
static inline uint16_t dif_uni(uint16_t h, uint16_t r) {
    uint16_t d = (h > r ? h - r : r - h) % 360;
    return d > 180 ? 360 - d : d;
}

// diferença dobrada em 0-90 — r e r+180 são equivalentes
static inline uint16_t dif_bi(uint16_t h, uint16_t r) {
    uint16_t d = (h > r ? h - r : r - h) % 180;
    return d > 90 ? 180 - d : d;
}

static bool sentido_compativel(const Radar* rd, double rumo_carro, double vel_kmh) {
    uint8_t dirtype = rd->flags & 0x03;

    if (dirtype == DIR_OMNI)     return true;   // 1.419 registros (7,8%)
    if (vel_kmh < VEL_MIN_RUMO)  return true;   // rumo indisponível → não descarta

    uint16_t h = ((uint16_t)(rumo_carro + 0.5)) % 360;
    uint16_t r = (uint16_t)rd->rumo * 2;

    return (dirtype == DIR_BI ? dif_bi(h, r) : dif_uni(h, r)) <= TOL_RUMO_GRAUS;
}
```

Note que `VEL_MIN_RUMO` faz o filtro **abrir**, não fechar: com rumo inválido, o
correto é não descartar nada. Descartar por um rumo que é ruído perderia radares reais.

### 3.3 Otimização disponível — 8 bytes, empacotado

Só se o orçamento apertar. O Brasil ocupa ~39° × 39°, que a 10⁻⁵ grau são ~3,9 M de
valores → **22 bits** por coordenada, como offset do canto SW:

```
lat_off 22 bits | lon_off 22 bits | limite 8 | rumo 8 | dirType 2 → 62 bits = 8 B
```

Economia: 18.294 × 4 = **71 KB**. O desempacotamento não pesa no caminho crítico: a
busca binária toca ~15 registros e a varredura de faixa desempacota no máximo 96 — nunca
os 18 mil (§4). Se for por aí, guarde a origem no cabeçalho e suba `versao` para 2.

---

## 4. Busca em dois estágios — custo medido (resolve R-03)

```c
#define RAIO_ALERTA_M    300
#define M_POR_GRAU_LAT   111320.0

void busca_radar_alvo(double lat, double lon, double rumo, double vel_kmh) {

    // ── Estágio 0: limiares, uma vez por fix — não por ponto ─────────────
    int32_t d_lat = (int32_t)(RAIO_ALERTA_M / M_POR_GRAU_LAT * 1e5);   // 269
    int32_t d_lon = (int32_t)(RAIO_ALERTA_M /
                              (M_POR_GRAU_LAT * cos(lat * DEG2RAD)) * 1e5);
    int32_t lat_q = (int32_t)(lat * 1e5);
    int32_t lon_q = (int32_t)(lon * 1e5);

    // ── Estágio 1: busca binária na faixa de latitude ────────────────────
    size_t i = limite_inferior_lat(base, n, lat_q - d_lat);   // ~15 comparações

    // ── Estágio 2: varredura da faixa, descarte inteiro por longitude ────
    const Radar* melhor = NULL;
    double melhor_dist = 1e9;

    for (; i < n && base[i].lat <= lat_q + d_lat; i++) {
        if (labs(base[i].lon - lon_q) > d_lon)                continue;  // inteiro
        if (!sentido_compativel(&base[i], rumo, vel_kmh))     continue;  // §3.2

        double d = haversine(lat, lon, base[i].lat * 1e-5, base[i].lon * 1e-5);
        if (d < melhor_dist) { melhor_dist = d; melhor = &base[i]; }
    }
    // ... condição de aproximação e histerese de zona (R-09)
}
```

### Custo real, medido sobre os 18.294 pontos

Simulando a busca a partir de **cada um dos 18.294 pontos** da base (ou seja, incluindo
os piores casos urbanos):

| Estágio | Mediana | p90 | p99 | **Máximo** |
| :--- | ---: | ---: | ---: | ---: |
| Candidatos na faixa de latitude | 11 | 49 | 78 | **96** |
| Sobreviventes → entram na Haversine | 2 | 4 | 8 | **24** |

Pior caso por ciclo: ~15 comparações de busca binária + 96 comparações inteiras +
**24 Haversines**. A 5 Hz isso é **120 Haversines por segundo** no pior cenário do país
— ruído no orçamento do RP2350, que roda a 150 MHz com unidade de ponto flutuante.

> **Ordenar por latitude e não longitude é deliberado:** o grau de latitude mede
> 111,32 km em qualquer ponto do globo, então o limiar da busca binária é uma constante.
> Em longitude ele dependeria de cos(lat) e o índice ficaria distorcido de norte a sul
> do país.

### 4.1 A FPU do RP2350 é de precisão simples — cuidado com a Haversine

Ponto não documentado em nenhum lugar do projeto e que vale registrar: **o Cortex-M33 do
RP2350 tem FPU de precisão simples (FPv5-SP)**. `double` é emulado em software, nas duas
linguagens.

A consequência não é só lentidão — é **perda de precisão**. Um `float` tem mantissa de
24 bits, ou seja ~7,2 dígitos decimais significativos. Uma coordenada como `-23.537216`
precisa de **8**. Calcular Haversine sobre coordenadas absolutas em `float` já começa
com o último dígito errado, e a Haversine ainda subtrai números próximos entre si
(cancelamento catastrófico) — o erro relativo cresce justamente em distâncias curtas,
que é todo o nosso caso de uso.

Três saídas, em ordem de preferência:

**1. Equirretangular sobre diferenças (recomendado).** Para 300 m o erro contra a
Haversine é inferior a 0,01%, e o cálculo opera sobre *diferenças* — números pequenos,
onde `float` tem precisão de sobra. O `cos(lat)` já está calculado uma vez por fix no
Estágio 0:

```c
// dlat/dlon em unidades de 1e-5 grau; cos_lat vem do Estágio 0
static inline float dist_m(int32_t dlat, int32_t dlon, float cos_lat) {
    float dy = dlat * (float)(M_POR_GRAU_LAT * 1e-5);
    float dx = dlon * (float)(M_POR_GRAU_LAT * 1e-5) * cos_lat;
    return sqrtf(dx*dx + dy*dy);
}
```

Custo: 1 `sqrtf` (instrução única na FPU) contra 2 `sin` + 2 `cos` + 1 `asin` + 1 `sqrt`
da Haversine. **5 a 8× mais barato**, e mais preciso neste regime.

**2. Haversine em `double`.** Correta, mas emulada em software. Pelos números da §4 cabe
no orçamento de qualquer forma — 24 cálculos por ciclo em 200 ms.

**3. Haversine em `float` sobre coordenadas absolutas.** ❌ **Não faça.** É a combinação
que parece certa e erra em silêncio.

> O RF02 pede Haversine explicitamente, e implementá-la é um bom exercício — comparar as
> duas numericamente a 300 m e a 50 km ensina bastante sobre erro de ponto flutuante.
> Só não a rode em precisão simples sobre valores absolutos.

---

## 5. Conversor e escrita de novos parsers

**A conversão roda fora do dispositivo.** Parsear 612 KB de texto e ordenar 18 mil
registros no RP2350 levaria minutos e desgastaria o cartão, sem ganho — o arquivo é
idêntico para todos os dispositivos.

```bash
python3 converte.py base_igo8.txt radares.bin
```

### 5.1 Contrato compartilhado — `formato_radares.py`

Todo o trabalho de formato binário está nesse módulo, não no parser. Um parser novo
produz uma lista de `Ponto` e chama `escreve()`:

```python
from formato_radares import Ponto, TipoPonto, Sentido, escreve

pontos = [
    Ponto(lat=-19.799916, lon=-44.021044, limite=30, rumo=257,
          tipo=TipoPonto.RADAR_FIXO, sentido=Sentido.UNIDIRECIONAL),
]
escreve(pontos, "radares.bin")
```

O `escreve()` cuida das quatro coisas que um parser erraria em silêncio se fizesse à
mão: quantização das coordenadas, empacotamento dos flags, **ordenação por latitude**
e CRC-32. A ordenação é a mais perigosa — a busca binária do firmware (§4) depende
dela e retorna resultado errado *sem avisar* se a invariante for violada.

O módulo também expõe `le()`, que relê com as **mesmas validações do firmware**. Use
para testar seu parser: se o `le()` aceita, o firmware carrega.

### 5.2 Enumerações

```python
class TipoPonto(IntEnum):
    RADAR_FIXO         = 1   # radar fixo de velocidade
    SEMAFORO_COM_RADAR = 2   # afere avanço de sinal E velocidade
    SEMAFORO_CAMERA    = 3   # câmera de sinal; NÃO afere velocidade
    RADAR_MOVEL        = 5   # ponto de operação de fiscalização móvel

class Sentido(IntEnum):
    OMNIDIRECIONAL = 0   # ignora o rumo; nunca descartar por sentido
    UNIDIRECIONAL  = 1   # vale só o rumo indicado
    BIDIRECIONAL   = 2   # vale o rumo indicado E o oposto
```

Os valores são os do padrão iGO8 e vão direto para o byte de flags, então são parte
do contrato binário — não os renumere sem subir a `VERSAO` do cabeçalho.

`TipoPonto` traz duas propriedades derivadas, para o firmware e para a tela não
reimplementarem a regra:

| Propriedade | Para que serve |
| :--- | :--- |
| `afere_velocidade` | `False` em `SEMAFORO_CAMERA`: sem limite, nunca entra em Zona de Perigo (RF03.3) |
| `e_semaforo` | `True` em ambos os semáforos: escolhe o ícone combinado (R-26) |

> ⚠️ O gatilho da Zona de Semáforo é **`limite == 0`**, não o tipo. Há registros de
> `SEMAFORO_CAMERA` **com** limite por inconsistência da base — 22 na base de
> referência. Usar o tipo como gatilho os trataria errado.

### 5.3 Parser de referência

O `converte.py` é o parser do padrão iGO8 e serve de modelo. Ele **rejeita** linha
inválida em vez de consertar em silêncio, listando número de linha e motivo, e sai
com código 1 se houver qualquer rejeição — inclusive `TipoPonto` ou `Sentido`
desconhecidos, que levantam `ValueError` no construtor do enum. Se uma atualização da
base introduzir um código novo, o conversor falha alto em vez de gravar lixo nos bits
de flags.

## 6. O framebuffer é o verdadeiro concorrente de memória

Não é óbvio e não aparecia em nenhum documento original:

```
240 × 240 pixels × 2 bytes (RGB565) = 115.200 B = 112,5 KB
```

O display consome **metade** do que a base consome, para uma função que não precisa de
memória persistente. Se o orçamento da §1 apertar, atacar aqui é mais barato que
comprimir dados:

- **Banda de 40 linhas:** 240 × 40 × 2 = 19 KB → **libera 96 KB**.
- A tela é composta em 6 passadas, enviando cada banda por SPI antes de gerar a próxima.
  Para esta interface (números grandes, poucas cores, 5 Hz) o custo é irrelevante.
- A Zona de Perigo pisca o fundo inteiro em vermelho — preencher a banda com cor
  constante é o caso mais barato que existe.

---

## 7. Consequências para os requisitos

### 7.1 🔴 `limite = 0` quebra a Zona de Perigo — **comportamento decidido**

**1.430 registros (7,8% da base) têm `SPEED = 0`** — são os **Semáforos c/ Câmera**
(`TYPE=3`), que fiscalizam avanço de sinal e **não aferem velocidade**, logo não têm
limite a registrar.

O RF03 define a Zona de Perigo como `Velocidade Carro > Limite Radar`. Com
`Limite = 0`, **qualquer** velocidade acima de zero satisfaz a condição: todo semáforo
dispararia fundo vermelho piscante e buzzer em "metralhadora" até o carro parar. Com
1.430 semáforos concentrados em área urbana, o aparelho ficaria inutilizável na cidade.

#### Decisão do autor (2026-09-15): Zona de Semáforo, silenciosa

Novo estado de operação, distinto das três zonas do RF03:

| Aspecto | Comportamento |
| :--- | :--- |
| **Gatilho** | `limite == 0` **e** distância ≤ 300 m **e** aproximando-se (R-09) |
| **Buzzer** | 🔇 **silencioso — sem exceção**, em qualquer velocidade |
| **Visor** | Alerta de semáforo com distância decrescente. **Sem placa de limite** (não há limite a exibir) |
| **LED RGB** | 🟡🔴 **amarelo e vermelho alternados** |
| **LED Wi-Fi** | apagado |
| **Zona de Perigo** | **nunca** — independente da velocidade |

Linha pronta para a matriz IHM do `requirements.md`:

```
| **Entrou no raio de 300m de semáforo** | Zona de Semáforo | Ícone de semáforo e distância decrescente em metros. Sem placa de limite. | **Amarelo/Vermelho alternados (2 Hz)** | Apagado | Silencioso |
```

#### ⚠️ Esta decisão depende do R-05 estar corrigido

O alerta escolhido é o único dos quatro estados que **exige o canal verde do LED RGB
funcionando**: amarelo é mistura de vermelho + verde. Com os 330 Ω do BOM atual o verde
fica em menos de 1 mA — praticamente invisível (R-05) — então a alternância
amarelo/vermelho apareceria como **vermelho constante**, indistinguível da Zona de
Perigo. Exatamente o oposto da intenção.

**Consequência:** o R-05 deixa de ser ajuste estético e passa a ser **pré-requisito
funcional** desta decisão. Corrigir antes de tentar validar o comportamento.

#### Implementação

A alternância é literalmente alternar um GPIO: o canal **vermelho fica aceso constante**
e o **verde liga e desliga**. Um dos estados de LED mais baratos do projeto.

```c
// Timer de hardware, conforme RNF04 — não bloquear o laço do GPS
#define SEMAFORO_PERIODO_MS  500      // 250 ms amarelo + 250 ms vermelho = 2 Hz
```

**Escolha de 2 Hz:** precisa ser distinguível do "Vermelho Piscante" da Zona de Perigo.
Se o perigo piscar a 4 Hz (rápido, urgente), a alternância do semáforo a 2 Hz lê como
"atenção" sem competir. Se as duas frequências ficarem próximas, o motorista não
distingue na visão periférica — que é justamente a função deste LED.

#### Prioridade entre alertas simultâneos

Em área urbana densa é comum ter semáforo e radar de velocidade dentro dos 300 m ao
mesmo tempo. A busca da §4 retorna o alvo mais próximo, o que faria um semáforo a 100 m
esconder uma infração iminente num radar a 280 m. Regra proposta:

1. **Zona de Perigo** (radar de velocidade com `vel > limite`) — sempre vence.
2. **Zona de Semáforo** e **Zona de Aproximação** — vence o mais próximo.

Exige rastrear o melhor candidato de **cada categoria** na varredura, não um único
`melhor`. Custo: dois ponteiros em vez de um, zero impacto de desempenho.

> Os itens 1 e 2 são recomendação minha, não decisão sua — a ordem entre semáforo e
> aproximação é chamada de produto e vale revisar com o aparelho rodando.

#### Caso de borda nos dados: os 22 combinados

22 registros têm `TYPE=3` (semáforo) **com** `SPEED` de 50 ou 60 — equipamentos
combinados de avanço de sinal e velocidade. Como a regra de gatilho é `limite == 0` e
**não** `TYPE == 3`, esses 22 caem corretamente no caminho de radar de velocidade, com
buzzer e Zona de Perigo normais. É o comportamento certo: eles realmente aferem
velocidade.

O campo `TYPE` nos flags do registro (§3.1) fica disponível para exibir um ícone
combinado, se você quiser distinguir na tela.

### 7.2 ❌ `TYPE=5` NÃO é trecho controlado — hipótese descartada

Esta seção afirmava que os 2.065 pontos de `TYPE=5` eram trechos controlados, que
aferem velocidade **média** entre dois pórticos, e concluía que comparar a velocidade
instantânea seria semanticamente incorreto.

**A hipótese está descartada.** O mapeamento oficial da §0.2 identifica `TYPE=5` como
**Radar Móvel** — fiscalização pontual, não por trecho. **Não existe aferição por
velocidade média nesta base.**

Consequências aplicadas:

| Item | Antes | Agora |
| :--- | :--- | :--- |
| `requirements.md` **RF03.5** | "Trecho controlado", com sinal de alerta próprio | Reescrito para **Radar Móvel** |
| `requirements.md` **RF03.10** | Mecanismo de velocidade média, escopo futuro | **Anulado** — sem dados sobre os quais operar |
| `revisao_tecnica.md` **R-20** | Premissa mista, média diferida | Resolvido: hipótese errada |

Fica registrado o que a correção *não* invalida: o zonamento de `TYPE=5` sempre foi
"radar de velocidade simples", que continua correto — Radar Móvel afere velocidade
instantânea como qualquer radar pontual.

### 7.3 🟠 `TYPE=2` é semáforo e não recebe aviso de semáforo

Revelado pelo mapeamento oficial: os **2.994 pontos de `TYPE=2` são Semáforos c/
Radar** — fiscalizam avanço de sinal *e* velocidade.

O gatilho da Zona de Semáforo (`requirements.md` RF03.3) é **`limite == 0`**. Como
esses pontos têm limite, caem no caminho de radar de velocidade e o motorista
**não recebe nenhuma indicação de que há fiscalização de avanço de sinal ali** — em
2.994 cruzamentos urbanos.

O campo `TYPE` preservado nos flags do registro (§3.1) permite corrigir sem mudar o
formato: o zonamento continua pela velocidade, e a tela exibe indicação combinada de
semáforo + radar. Registrado em `revisao_tecnica.md` como **R-26**, para a sessão
dedicada a telas.

## 8. Pontos abertos

### ✅ A-01 — Mapeamento dos campos do iGO8 — **RESOLVIDO**

Cabeçalho explícito no arquivo, semântica do `DirType` confirmada por análise
geométrica (§0.1), conversor escrito e verificado com round-trip. Resta apenas a
ressalva de rótulo da §0.2, que não afeta o firmware.

### ❓ A-02 — Linguagem e runtime não definidos

Nenhum documento do projeto declara se o firmware é **C/C++ (Pico SDK)** ou
**MicroPython**. Para esta arquitetura a escolha **não é livre**.

**Análise completa na §9.** Resumo: velocidade não é o critério (MicroPython usaria ~4%
do orçamento de ciclo); **memória e dual core** são. O `_thread` do port RP2 tem GIL,
então a divisão core 0 = GPS / core 1 = UI exigida pelo RNF04 e pelo R-15 não se realiza
em MicroPython.

**Recomendação: C/C++ com o Pico SDK**, registrado como requisito não-funcional.
Decisão de maior alavancagem em aberto — resolve ou reabre o R-02.

**Antes de fechar:** rodar o teste de heap da §9.5 no hardware real.

### ⚠️ A-03 — Estimativas a medir

Os itens marcados ⚠️ na tabela da §1 (pilha lwIP/CYW43, runtime) são estimativas. Medir
com `arm-none-eabi-size` no binário real e com marca d'água de stack antes de considerar
o orçamento fechado.

---

## 9. Escolha de linguagem: C/C++ vs MicroPython (fundamenta o A-02)

> ⚠️ Os números desta seção são **estimativas raciocinadas, não medições** — não houve
> acesso ao hardware. A conclusão é robusta à margem de erro, mas o teste da §9.5
> resolve a dúvida em 30 segundos e deve ser feito antes de fechar a decisão.

### 9.1 Velocidade não é o critério

Orçamento de 200 ms por ciclo a 5 Hz. Pior caso medido na §4: 15 comparações de busca
binária + 96 comparações inteiras + 24 cálculos de distância.

| Etapa (pior caso) | C / Pico SDK | MicroPython | MicroPython + `@viper` |
| :--- | ---: | ---: | ---: |
| Parse `$GNRMC` (~80 B) | ~20 µs | ~1–2 ms | — |
| Busca binária (~15 comp.) | <5 µs | ~150 µs | ~10 µs |
| Varredura de faixa (96 reg.) | ~10 µs | ~2–3 ms | ~50 µs |
| 24 × cálculo de distância | ~150 µs | ~1–2 ms | — |
| **Total do ciclo crítico** | **~0,2 ms (0,1%)** | **~5–8 ms (3–4%)** | **~2–3 ms** |

Mesmo errando por 3×, MicroPython usaria ~10% do orçamento. **O algoritmo de dois
estágios é eficiente o bastante para tornar a linguagem irrelevante neste eixo.**

E há uma ironia útil: o custo dominante do ciclo não é o cálculo, é o display. Empurrar
115 KB de framebuffer por SPI a 32 MHz leva **~29 ms** — 14% do ciclo, quase 150× o
custo da busca em C. É limite de banda de barramento, idêntico nas duas linguagens
(embora em C o DMA o tire do caminho crítico).

### 9.2 Memória é o critério

| Consumidor | C / Pico SDK | MicroPython |
| :--- | ---: | ---: |
| Base 18.294 × 12 B | 214 KB estático em `.bss` | 214 KB em `bytearray` **contíguo** |
| Framebuffer cheio | 115 KB | 115 KB |
| Runtime / interpretador | ~20 KB | ⚠️ **~80–120 KB** |
| lwIP + CYW43 | ~48 KB | ~48 KB |
| **Sobra de 520 KB** | **~111 KB** | ⚠️ **perto de zero ou negativo** |

Dois detalhes que pesam mais que o total:

**Nunca guardar os radares como objetos Python.** Uma tupla de 4 inteiros custa ~100 B
com header e ponteiros — 18.294 delas seriam ~1,8 MB, inviável por 3,5×. O caminho
obrigatório é um `bytearray` único com `struct.unpack_from` / `memoryview`, indexando por
offset. Ou seja: em MicroPython você escreve com a disciplina de C **sem** ganhar o
desempenho de C.

**Alocação contígua de 214 KB é frágil.** Em C, `static Radar base[18294];` vive no
`.bss` e é garantida no link. Em MicroPython depende do estado da heap — e após horas de
operação, com fragmentação, um `bytearray` grande pode falhar mesmo havendo total livre
suficiente. Alocar no boot e nunca liberar.

### 9.3 Três coisas que quebram de verdade em MicroPython

1. **Dual core não funciona como o projeto precisa.** O RNF04 e o R-15 assumem core 0 =
   GPS/cálculo e core 1 = UI. O `_thread` do port RP2 tem GIL: os dois cores não executam
   bytecode Python em paralelo de verdade. Em C, `multicore_launch_core1()` dá
   paralelismo real. **É o ponto mais grave**, porque contraria um requisito explícito,
   não uma otimização.
2. **Pausas de GC injetam jitter.** O coletor pode parar tudo por alguns a algumas
   dezenas de ms, de forma imprevisível. Com 200 ms de orçamento sobrevive, mas é jitter
   não determinístico num alerta de segurança. Mitigável com zero alocação no laço quente
   e `gc.collect()` em pontos controlados — disciplina permanente.
3. **Latência de IRQ.** Handlers são callbacks Python com restrição de alocação, latência
   de dezenas de µs e jitter. Para o KY-040 em velocidade humana, suficiente. Para
   qualquer coisa mais apertada, não.

### 9.4 O que MicroPython ganha

Não é pouco, e num projeto de estudo pesa: REPL interativo, sem toolchain, sem ciclo de
flash, teste do parser NMEA colando strings direto no prompt. Velocidade de iteração
provavelmente 3–5× maior. Para explorar a lógica de zonas e a UX, é valor real.

**Caminho intermediário:** `@micropython.viper` compila um subconjunto tipado para código
de máquina, com aritmética inteira nativa e acesso via `ptr32`/`ptr8`. Aplicado só à
varredura de faixa, tira a parte lenta do caminho crítico e mantém o resto em Python.

### 9.5 Teste que fecha a decisão com dado

O elo fraco desta análise é o tamanho real da heap do MicroPython no RP2350. Grave o
MicroPython no Pico 2 W e rode no REPL:

```python
import gc
gc.collect()
print("livre:", gc.mem_free(), "usado:", gc.mem_alloc())
base = bytearray(219528)        # a base de radares
fb   = bytearray(115200)        # framebuffer cheio
print("apos alocar:", gc.mem_free())
```

Se sobrar menos de ~60 KB depois das duas alocações, a decisão está tomada sozinha.

### 9.6 Recomendação

**C com o Pico SDK**, principalmente pelo dual core (§9.3, item 1) — é requisito do
projeto e MicroPython não entrega. Memória em segundo lugar.

Ressalva honesta: se o objetivo de aprendizado estiver mais na lógica geográfica e na UX
do que em embarcados de baixo nível, **MicroPython é viável** aceitando renderização em
bandas (19 KB em vez de 115 KB) e abrindo mão do paralelismo real. A base de 214 KB
cabe; o framebuffer cheio é que não.

---

## Anexo A — Plano B: particionamento em quadrantes

**Não implementar agora.** Registrado para o caso de a base crescer além de ~35.000
registros, ou de a escolha de runtime (A-02) comer o orçamento.

- **Quadrante de 0,5°** (~55 km), grade de 78 × 78 = 6.084.
- **Caixa envolvente:** usar a **política** do Brasil (lat −33,75…+5,3; lon −74,0…−34,8)
  e não a dos dados atuais (lat −32,68…+3,07; lon −67,96…−34,82). A caixa dos dados é
  mais estreita, mas um radar novo no Acre ou no extremo sul cairia fora dela — validar
  na carga e rejeitar, nunca truncar.
- **Margem sobreposta de 1 km** em cada borda: cada arquivo carrega também os pontos
  logo fora do seu quadrante. Elimina por construção o problema do canto (onde seriam
  necessários 4 quadrantes) e o thrashing em estradas que correm sobre a divisa. Custo:
  `(57² − 55²)/55² = 7,4%` de duplicação.
- **Coordenadas relativas em `uint16`:** quadrante de 0,5° + 2 × 0,01° de margem =
  0,52° = 52.000 na escala 10⁻⁵, que cabe em `uint16` (65.535) com 26% de folga.
  Registro cai para 8 bytes. 0,5° é o maior quadrante em que isso ainda fecha.
- **Um único arquivo com índice no cabeçalho**, não 6.084 arquivos:
  `[cabeçalho][índice 6.084 × u32 de offset][registros agrupados por quadrante]`.
  Índice de 24 KB residente; `count = (off[i+1] − off[i]) / 8`. Carregar um quadrante =
  um `seek` + um `read`, sem varredura de diretório FAT32 — que é a parte lenta do
  acesso a SD e seria paga a cada cruzamento. Mantém o OTA atômico como um `rename` só;
  com milhares de arquivos não existe atualização atômica da base.
- **Duplo buffer com prefetch projetado pelo rumo:** o alvo é o quadrante em que cai
  `posição + rumo × velocidade × 60 s`, não "o próximo de uma lista". Com rumo inválido
  por baixa velocidade (R-04), não faz prefetch — a margem cobre até o cruzamento real.
  Carga no core 1, GPS segue no core 0 (R-15).
- **Histerese de 200 m** além da fronteira para trocar o quadrante ativo.

Uma grade é 2D e cada célula tem 8 vizinhos, então a topologia não é uma lista encadeada
circular: é caixa envolvente + projeção de rumo. Manter "ativo + próximo" está correto
como mecanismo de buffer; apenas a escolha de qual é o "próximo" precisa vir do rumo.
