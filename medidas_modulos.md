# 📏 Medidas dos módulos — para os footprints próprios

**Instrumento:** paquímetro.
**Objetivo:** substituir a barra de pinos genérica por footprint com o
**contorno** de cada módulo, para que o posicionamento na placa acuse
sobreposição. Ver `pcb.md` §7.

> 📦 **Os números não moram mais aqui.** Desde 2026-10-05 as medidas vivem num
> arquivo legível por programa, junto da cadeia de geração, que não é
> distribuída com este repositório. O que ficou aqui é o que tem valor
> independente da ferramenta: **o protocolo de medição e as lições**.
>
> As tabelas da seção 4 continuam como formulário — servem para medir um módulo
> novo antes de os números irem para lá.

> 📌 Enquanto uma linha estiver vazia, ela é **desconhecida** — não presumida.
> Nada aqui deve ser preenchido por datasheet ou por foto. O que não foi medido
> fica em branco, e o footprint correspondente não é gerado.

---

## 1. Como medir

### Origem: o centro do pino 1

Todas as distâncias saem do **centro do pino 1**. É o único ponto que a placa e
o módulo têm em comum. Medir cada borda de forma independente faz os erros
somarem, e o furo sai fora.

### Passo: medir o vão inteiro, nunca entre vizinhos

Entre dois pinos adjacentes, ±0,1 mm de leitura vira ±4 % de erro. Meça do
**centro do pino 1 ao centro do último** e divida pelo número de intervalos:

```
8 pinos  →  7 intervalos  →  17,78 mm / 7 = 2,54 mm   ✅ confirmado
```

Isso divide o erro de leitura pelo número de intervalos.

### Centro de furo

Não se mede direto. Meça entre dois furos **por fora** (borda externa a borda
externa) e **por dentro** (borda interna a borda interna). A média das duas é a
distância entre centros, e a diferença é o diâmetro somado dos dois.

### Precisão

Duas casas decimais, em milímetros. Se a leitura oscilar entre duas posições do
paquímetro, anote as duas — a incerteza é informação, o número redondo
inventado não é.

---

## 2. O que medir em TODO módulo

| # | Medida | Para quê |
| :-- | :--- | :--- |
| 1 | **Comprimento × largura** da placa | contorno e detecção de sobreposição |
| 2 | **Vão do pino 1 ao último**, e quantos pinos | confirma o passo real |
| 3 | Do centro do pino 1 à **borda mais próxima** no eixo da fileira | posição da fileira no contorno |
| 4 | Do centro do pino 1 à **borda lateral** (perpendicular à fileira) | idem, no outro eixo |
| 5 | **Altura total**, incluindo o componente mais alto | folga dentro da caixa |
| 6 | Furos de fixação: **diâmetro** e posição a partir do pino 1 | só se formos parafusar |
| 7 | A fileira é de **uma linha só** ou há mais? | número de fileiras |

⚠️ **Diga também de que lado os pinos saem** — se a fileira está na borda com a
face dos componentes para cima, ou do lado oposto. Isso define se o módulo
encaixa virado para cima ou para baixo, e um footprint espelhado é erro que não
aparece até a placa chegar.

---

## 3. O que cada módulo tem de particular

### GPS GY-GPS6MV2 (NEO-M8N) — 4 pinos

- Posição do **conector U.FL** a partir do pino 1, nos dois eixos.
  É o que decide se o rabicho de **8 cm** alcança a antena (R-67, `montagem.md`
  §4.8). Se o U.FL cair do lado errado da placa, o cabo não chega — e são 30
  ciclos de encaixe, não é conector de manutenção.
- Para que lado o U.FL **aponta**.

### Leitor microSD — 9 pinos

- Direção da **boca do cartão** em relação à fileira de pinos.
  O cartão precisa entrar e sair com a caixa fechada ou semiaberta; se a boca
  ficar para dentro, o leitor é inútil montado.
- Quanto o soquete **avança além da borda** da placa, se avançar.

### Display ST7789V 2,4" — 8 pinos

- **Área visível** do vidro (não a placa): largura × altura.
- Posição da área visível a partir do pino 1 — é o que define o recorte na face
  frontal da caixa.
- Espessura total com o vidro.

### Encoder KY-040 — 5 pinos

- **Diâmetro do eixo** e comprimento acima da placa.
- Posição do eixo a partir do pino 1 — define o furo no painel.
- Diâmetro da **rosca de fixação**, se houver.

### Conversor LM2596 (12 V → 5 V) — 4 pinos

- Altura do **indutor**, que costuma ser a peça mais alta de todas.
- Se os 4 pinos são uma fileira só ou dois pares separados.

---

## 4. As medidas

### 4.1 GPS GY-GPS6MV2

| Medida | Valor (mm) |
| :--- | :--- |
| Comprimento × largura | |
| Vão pino 1 → pino 4 | |
| Passo calculado | |
| Pino 1 → borda (eixo da fileira) | |
| Pino 1 → borda lateral | |
| Altura total | |
| U.FL: posição a partir do pino 1 | |
| U.FL: para onde aponta | |
| Furos de fixação | |

### 4.2 Leitor microSD

| Medida | Valor (mm) |
| :--- | :--- |
| Comprimento × largura | |
| Vão pino 1 → pino 9 | |
| Passo calculado | |
| Pino 1 → borda (eixo da fileira) | |
| Pino 1 → borda lateral | |
| Altura total | |
| Direção da boca do cartão | |
| Avanço do soquete além da borda | |
| Furos de fixação | |

### 4.3 Display ST7789V 2,4"

| Medida | Valor (mm) |
| :--- | :--- |
| Comprimento × largura da placa | |
| Vão pino 1 → pino 8 | |
| Passo calculado | |
| Pino 1 → borda (eixo da fileira) | |
| Pino 1 → borda lateral | |
| Área visível do vidro | |
| Área visível: posição a partir do pino 1 | |
| Espessura total | |
| Furos de fixação | |

### 4.4 Encoder KY-040

| Medida | Valor (mm) |
| :--- | :--- |
| Comprimento × largura | |
| Vão pino 1 → pino 5 | |
| Passo calculado | |
| Pino 1 → borda (eixo da fileira) | |
| Pino 1 → borda lateral | |
| Altura total | |
| Eixo: diâmetro e comprimento | |
| Eixo: posição a partir do pino 1 | |
| Rosca de fixação | |

### 4.5 Conversor LM2596

| Medida | Valor (mm) |
| :--- | :--- |
| Comprimento × largura | |
| Vão entre os pinos | |
| Passo calculado | |
| Pino 1 → borda (eixo da fileira) | |
| Pino 1 → borda lateral | |
| Altura total (indutor) | |
| Arranjo dos 4 pinos | |
| Furos de fixação | |

---

## 5. Pendente de decisão, não de medida

O `montagem.md` põe **display e encoder na face frontal**, voltados ao
motorista. Se os dois forem fixados na caixa e não na placa, a barra de pinos
deixa de ser "onde o módulo encaixa" e passa a ser "conector para o chicote" —
e aí o contorno do módulo não vai para a placa, vai para o desenho da caixa.

As medidas servem aos dois casos, então vale medir de qualquer jeito. Mas a
decisão precede o contorno da placa.
