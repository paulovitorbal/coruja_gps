# 0006 — O orçamento de memória é verificado pelo compilador e pelo linker

*Data: 2026-09-17 · Estado: aceita*

## Contexto

O orçamento de memória do `formato_dados.md` §1 é a restrição central do
projeto: 520 KiB de SRAM, com a base de radares e o framebuffer disputando a
maior parte. Até aqui ele era **estimado no papel**, e a primeira compilação
para ARM mostrou que duas coisas estavam erradas — nenhuma delas visível nos 92
testes de host, porque nenhuma é um erro de lógica.

**Primeiro: `sizeof(Ponto)` era 16 bytes, não 12.** O registro em arquivo tem
12 bytes, e eu havia expandido o rumo para `uint16_t` em graus por conveniência
de leitura. O padding de alinhamento empurrou a struct para 16. Consequência: a
base real passaria de 214,4 para **285,8 KiB**, e o teto de 40.000 pontos daria
625 KiB — **mais que a SRAM inteira**. O "teto de proteção de memória" não
protegia nada.

**Segundo: o array estático estava dimensionado pelo teto do formato.** Reservar
40.000 pontos custa 468,75 KiB e deixa **48,3 KiB livres** de 520 — medido no
linker, não estimado. Não sobra para lwIP, stacks e as bandas do display.

## Decisão

**Fechar o `Ponto` em 12 bytes**, mantendo o rumo quantizado como no arquivo,
com um acessor `rumo_graus()` que multiplica por 2. A quantização é irrelevante
para o RF02.3, cujo filtro de sentido tem tolerância de dezenas de graus.

**Separar duas constantes que eram uma:**

| Constante | Valor | Significa |
| :--- | ---: | :--- |
| `kTetoPontos` | 40.000 | teto do **formato**: acima disso o arquivo é absurdo |
| `kCapacidadeFirmware` | 24.000 | o que **esta placa** reserva em `.bss` |

**E deixar o compilador guardar os invariantes**, em vez da disciplina de quem
lê o código:

```cpp
static_assert(sizeof(Ponto) == kTamRegistro, ...);
static_assert(kCapacidadeFirmware * sizeof(Ponto) < 355u * 1024u, ...);
```

## Consequências

* Medido no linker depois da correção: **284,2 KiB de SRAM, 235,8 KiB livres**,
  e 66,6 KiB de flash de 4096. O orçamento do §1 deixa 355 KiB para a base;
  24.000 pontos ocupam 281,2 KiB e dão **31% de folga** sobre os 18.294 de hoje.
* Um arquivo entre 24.000 e 40.000 pontos é **válido pelo formato e recusado
  por este firmware**, com `ExcedeuCapacidade`. A distinção é correta e já
  existia na assinatura do carregador, que sempre recebeu a capacidade como
  parâmetro: o arquivo não está corrompido, está grande demais para esta placa.
* Reordenar campos do `Ponto` ou trocar um tipo por um maior agora **quebra o
  build**, não o orçamento em silêncio. Era o único jeito de tornar a
  restrição durável — um comentário pedindo cuidado não sobrevive a um
  refactor bem-intencionado que "melhora a legibilidade" do rumo.
* Se a base crescer além de ~24.000 pontos, a saída não é subir a constante sem
  pensar: é o empacotamento em 8 bytes da §3.3, que libera 71 KiB.

## Nota sobre como o erro escapou

O array só era referenciado por `sizeof`, e `sizeof` não exige que o objeto
exista — o linker eliminou `g_pontos` inteiro, e o primeiro relatório de
memória dizia **2,7 KiB de `.bss` e 517 KiB livres**, número que era ficção.
O `main.cpp` agora chama `carrega_base()` de verdade, o que referencia o array
e, de passagem, exercita o caminho de Falha de Dados do RF07.

**Medir uma reserva que o linker removeu não mede nada.**
