# ADR 0012 — Atualização do firmware pela rede: partições A/B no RP2350

**Data:** 2026-11-16
**Status:** **proposta** — nada implementado; as premissas marcadas com 🔬 pedem medição ou
conferência no aparelho antes de virar código.

> Rascunho redigido com apoio de IA. É sugestão sujeita à revisão e aos testes de bancada do autor.

## Contexto

Hoje o firmware só é atualizado gravando um UF2 com o `BOOTSEL` apertado e o cabo USB ligado. Com o
aparelho na caixa, isso exige abri-la.

O item "atualizar" do menu já existe, mas atualiza a **base de radares** (`radares.bin`, ~214 KB) pelo
`AtualizadorOta`. Ele não toca no firmware. A proposta é um segundo caminho, para o **firmware**.

O que o projeto já tem e este ADR reaproveita:

| Peça | Onde |
| :--- | :--- |
| Wi-Fi com credenciais lidas do cartão (RNF03) | `RedeWifi`, ADR 0002 |
| TLS com raízes confiáveis | `ClienteTls`, `RaizesConfiaveis.h` |
| Download com verificação e prazo de progresso | `Baixador`, `VerificadorDownload`, `PrazoDeProgresso` |
| Tela e LED durante a operação bloqueante | `OtaNaTela`, `TelaOta` |
| Recusa em movimento ("PARE PARA ATUALIZAR") | `main.cpp`, `AcoesDoAparelho` |
| Identificação de build regenerada a cada compilação | `cmake/gera_versao.cmake` |

Números de hoje: `coruja_gps.bin` com **715 KB** (`text` 742 KB), flash de **4 MB** (Pico 2 W,
`PICO_BOARD=pico2_w`), Pico SDK **2.3.1**. O UF2 cresceu cerca de 40 KB em 12 versões
(v0.2.11 → v0.2.23).

O SDK 2.3.1 expõe as funções do bootrom do RP2350 que sustentam este desenho:
`rom_pick_ab_partition`, `rom_pick_ab_partition_during_update`, `rom_explicit_buy` e o reboot do tipo
`REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE` (conferido em `pico_bootrom/include/pico/bootrom.h`).

### O risco que governa a decisão

Gravar firmware é a única operação do aparelho que, se interrompida, o deixa **inutilizável**. O
aparelho é alimentado pelo carro (ADR 0009): a energia pode cair a qualquer momento, inclusive no meio
da gravação. O OTA da base não tem esse problema porque, no pior caso, sobra a base anterior
(`radares.bak`). O firmware precisa da mesma propriedade: **sempre sobra uma imagem boa**.

## Decisão

### 1. Duas partições de imagem (A/B) na flash, escolhidas pelo bootrom

Uma tabela de partições do RP2350 com dois slots de firmware do mesmo tamanho. A atualização grava no
slot **inativo** e nunca mexe no que está rodando.

* **Tamanho do slot: 1,5 MiB** (a imagem de hoje ocupa menos da metade). A folga custa flash, que sobra:
  2 × 1,5 MiB deixam ~1 MiB livre dos 4 MiB.
* 🔬 Conferir que nada além do firmware usa o fim da flash. Pelo ADR 0002 a configuração vive no cartão,
  então a expectativa é que não.

### 2. A imagem vai direto para o slot inativo, sem passar pelo cartão

O cartão pode estar ausente ou ilegível, e o ADR 0010 trata os dois como o mesmo caso. Se o OTA de
firmware dependesse dele, ganharia um modo de falha novo sem necessidade. A imagem é gravada em fluxo,
setor a setor, enquanto o SHA-256 é calculado.

As credenciais de Wi-Fi continuam vindo do cartão (RNF03), então o cartão segue necessário **para
conectar** — só não para guardar a imagem.

O formato baixado é o **mesmo UF2** gravado por cabo, como no exemplo oficial
`pico-examples/pico_w/wifi/ota_update`. Um artefato só para os dois caminhos evita publicar pela rede
algo diferente do que foi testado na bancada. O custo é baixar o dobro (o UF2 leva 256 B de carga em
cada bloco de 512 B: ~1,4 MB contra os ~715 KB do `.bin`).

### 3. Integridade: manifesto pequeno sobre TLS, com SHA-256

O servidor publica um manifesto (versão, tamanho, SHA-256). O aparelho baixa a imagem, compara tamanho e
hash **antes** de pedir ao bootrom que a considere. Imagem que não confere é descartada e o slot inativo
fica sem uso.

* **Não** se habilita *secure boot* do RP2350. Ele grava em OTP, é irreversível e não se justifica num
  projeto de estudo.
* **Assinatura da imagem** (Ed25519 ou ECDSA) fica **fora** desta decisão. Sem ela, quem controlar o
  servidor controla o código do aparelho. É um risco conhecido e aceito enquanto o servidor for do
  próprio autor; se mudar, vira ADR novo que supera este ponto.

### 4. Experimentar antes de confirmar (TBYB), com reversão automática

Depois de gravar e verificar, o aparelho reinicia no slot novo em modo *try-before-you-buy*. A imagem
nova precisa chamar `rom_explicit_buy()` para se confirmar. Se reiniciar antes (watchdog, queda de
energia, travamento), o bootrom volta ao slot anterior.

**O prazo não é nosso: o bootrom dá 16,7 s.** Ao subir uma imagem TBYB ele arma o watchdog, e se a
compra não sair nesse tempo o aparelho reinicia na imagem anterior (README do exemplo oficial
`pico-examples/pico_w/wifi/ota_update`, que remete ao §5.1.17 do datasheet do RP2350). O critério
precisa caber nessa janela com folga.

**Critério de confirmação** — a imagem nova só se confirma quando, **em até 10 s depois do boot**:

1. o display inicializou;
2. o GPS entregou ao menos uma sentença NMEA válida. Basta a sentença, **não** o fix: dentro de casa
   não há fix, e exigi-lo faria toda atualização reverter.

O critério 2 existe porque prova o caminho UART do GPS, que é o que uma imagem quebrada mais provavelmente
perde. O módulo emite NMEA antes de ter fix, então o critério não herda a variação do primeiro fix.
Prazo esgotado sem os dois critérios: **não compra**, e o watchdog do bootrom faz a reversão.

* O firmware **não pode mexer no watchdog** antes de comprar. Desligá-lo ou reprogramá-lo anula a
  reversão.
* Consequência aceita: em bancada sem GPS montado, toda atualização de firmware reverte.
* 🔬 Medir o tempo do boot até a primeira sentença NMEA. A partida de 15/10/2026 levava 6,0 s até a
  tela de dirigir, antes de tirar 1,5 s de espera pelo console.

### 5. Item de menu separado e com as mesmas travas

* **Item próprio**, distinto de "atualiza base". Firmware e base têm riscos e durações diferentes, e
  juntar os dois esconderia o mais perigoso atrás do mais frequente.
* **Só parado**: reaproveita a recusa "PARE PARA ATUALIZAR".
* **Confirmação em dois passos** no encoder. Atualizar a base é um clique; trocar o firmware não deveria
  ser.
* Progresso na tela e no LED pela `OtaNaTela`, trocando os rótulos como a `RemessaNaTela` já faz.
* **Recusa se a versão do servidor não for maior** que a rodando.

### 6. Versão monotônica

Num boot normal, o bootrom escolhe entre A e B pela versão gravada no bloco da imagem. *Downgrade* é
possível, mas só pelo caminho da atualização: o boot de *flash update* dá preferência à partição recém
gravada, e o `rom_explicit_buy()` de uma versão menor apaga o primeiro setor da outra partição para que
ela não volte a ser escolhida. Hoje a identificação de build (`gera_versao.cmake`) é texto para
humanos. A atualização precisa de um **número que só cresce**, derivado da tag e da contagem de commits
depois dela (o plano de implementação detalha a conta). 🔬 Conferir o que o bootrom faz com duas
versões **iguais**.

## Alternativas descartadas

| Alternativa | Por que não |
| :--- | :--- |
| **Sobrescrever o firmware em execução** com a imagem vinda do cartão | Queda de energia no meio deixa o aparelho sem firmware, sem caminho de volta que não seja o `BOOTSEL`. É o risco que a decisão existe para evitar. |
| **Bootloader próprio** que lê o `.bin` do cartão e grava | Reinventa o que o bootrom do RP2350 já faz, e ainda traz um segundo programa para manter e testar. Mesmo raciocínio do ADR 0007. |
| **Tudo em um passo, sem TBYB** | A imagem pode passar no hash e ainda assim travar ao subir. Sem reversão, o aparelho fica preso no slot ruim. |
| **Secure boot** | Irreversível (OTP) e fora de proporção para o projeto. |
| **Não fazer** | É opção legítima: o `BOOTSEL` funciona. O custo é abrir a caixa a cada versão, o que já pesa com a montagem fechada. |

## Consequências

**Ganha-se:**

* Atualizar o firmware sem abrir a caixa e sem cabo.
* Queda de energia durante a gravação não inutiliza o aparelho: o slot ativo nunca é tocado.
* Imagem quebrada que trava ao subir reverte sozinha.

**Paga-se:**

* **Migração única por cabo.** A primeira gravação precisa instalar a tabela de partições pelo
  `BOOTSEL` (`picotool partition create`, gravar a tabela, reiniciar, gravar o firmware). Depois disso
  as atualizações vão pela rede. O UF2 continua servindo para a gravação por cabo: com a tabela
  instalada, o bootrom o grava na partição inativa e sobe a imagem em modo TBYB, então **a compra
  também acontece depois de gravar por cabo**.
* **Só o firmware do carro é TBYB.** As bancadas (`CORUJA_DIAGNOSTICO`, `CORUJA_BANCADA_*`) não
  compram, e com a marca TBYB reverteriam sozinhas depois de 16,7 s.
* **Metade da flash por imagem.** Sobra espaço, mas o teto de cada slot (1,5 MiB) passa a ser um limite
  de projeto, a acompanhar como o ADR 0006 acompanha a RAM.
* **Gravar flash trava a execução por instantes.** Com a flash em apagamento/gravação, rotinas de
  interrupção que vivem na flash não podem rodar. O OTA da base já é bloqueante e só roda parado, então
  a travada é aceitável. O rádio não é o problema: o projeto usa `pico_cyw43_arch_lwip_poll`, e sem
  `cyw43_arch_poll()` nada do CYW43 roda. As outras interrupções (amostragem do encoder a 1 ms, USB)
  ficam desligadas em volta de cada operação de flash. 🔬 Medir se o TCP perde dados durante os
  apagamentos.
* **O TLS já é o maior consumidor de RAM** (medido por `anota_consumo`). A gravação em fluxo mantém só um
  setor (4 KiB) por vez, mas 🔬 o pico do TLS com o buffer de setor somado precisa ser conferido contra o
  orçamento do ADR 0006.
* **Servidor ganha um endpoint** (manifesto + imagem), em `servidor/`.

## Como testar

Seguindo o ADR 0001, o que dá para provar no host se prova no host; o resto é bancada.

* **No host:** o orquestrador (baixar → verificar → gravar → pedir reboot) atrás de uma interface
  `GravadorDeImagem`, como o `Baixador` hoje, com um gravador falso. Cobrem-se: hash errado, tamanho
  errado, queda no meio, servidor com versão igual ou menor, falha de gravação.
* **No aparelho:** uma bancada nos moldes de `bancada_ota.cpp`, e a **campanha de queda de energia** —
  cortar a alimentação em pontos diferentes da gravação e conferir que o aparelho sobe sempre. Este
  teste não tem substituto: o bootrom só roda no silício.
* **Reversão:** uma imagem de teste que deliberadamente não se confirma, para provar que o aparelho volta
  ao slot anterior.

## Ordem sugerida de implementação

1. Migração: tabela A/B instalada por cabo, aparelho rodando normalmente do slot A. Sem nenhum OTA ainda.
2. Bancada de gravação no slot B a partir de uma imagem conhecida, sem rede.
3. Rede: manifesto, download em fluxo, hash.
4. TBYB e critério de confirmação.
5. Item de menu e confirmação em dois passos.
6. Campanha de queda de energia.

Cada passo deixa o aparelho utilizável se o seguinte falhar.
