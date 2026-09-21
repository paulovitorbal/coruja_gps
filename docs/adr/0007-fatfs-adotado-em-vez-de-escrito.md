# ADR 0007 — FAT32 por biblioteca adotada, e a partição escolhida pelo arquivo

**Data:** 2026-09-20
**Status:** aceito

## Contexto

O `coruja.cfg` e o `radares.bin` vivem num cartão microSD formatado em FAT32
(RNF03). Ler FAT32 significa: driver SPI para o cartão, camada de blocos,
tabela de alocação, diretório, nomes longos. Nada disso é específico deste
projeto, e tudo já foi escrito e depurado por outros.

## Decisão

Adotar **`carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico` v3.6.2**, puxada por
`FetchContent`. Apache-2.0, mantida, com suporte a RP2350 já no código.

Dois detalhes que pesaram além do "funciona":

* **Ela já trata card detect** (`use_card_detect`, `card_detect_gpio`,
  `card_detected_true`), inclusive escolhendo o pull interno pela polaridade.
  Era código que eu já tinha começado a escrever e joguei fora.
* A configuração de hardware é um arquivo do **nosso** lado
  (`armazenamento/hw_config.cpp`), então os pinos continuam saindo do
  `Pinos.h` em vez de virarem uma segunda cópia livre para divergir.

## A partição é escolhida pelo arquivo, não pelo tipo

O padrão do FatFs (`FF_MULTI_PARTITION = 0`) monta **a primeira partição FAT
que encontrar**. O critério vira *"isto é FAT?"*, nunca *"isto tem o
`coruja.cfg`?"*. Num cartão de duas partições com a configuração na segunda, o
aparelho diria **"sem configuração"** com o arquivo fisicamente no cartão — e
o `DET` dizendo "cartão presente", e o mount dizendo "ok". Ver R-38.

Então: `FF_MULTI_PARTITION = 1`, `FF_VOLUMES = 5`, e uma `VolToPart[]` com o
volume 0 em detecção automática (cartão de uma partição ou sem MBR, que é o
caso comum) e os volumes 1 a 4 forçados nas quatro entradas primárias da MBR.
A sondagem percorre os cinco **procurando o arquivo** e fica no primeiro que o
tiver.

Custo: até cinco tentativas de mount, uma vez por clique. Nenhum custo de RAM
proporcional — o objeto `FATFS` é um só, reaproveitado, porque as tentativas
são sequenciais.

Fora do alcance: partição lógica dentro de estendida. A MBR tem quatro
entradas primárias e o FatFs não percorre a cadeia de estendidas. Fica
documentado em vez de descoberto em campo.

## O override do `ffconf.h`, e por que ele tem um guarda

Essas duas constantes moram no `ffconf.h` da biblioteca. A sobrescrita é pelo
caminho de include — `target_include_directories(... BEFORE INTERFACE ...)`,
que é o mecanismo que a própria biblioteca usa nos exemplos.

O risco disso é específico e desagradável: se a ordem se perder numa mudança
futura de CMake, **o código continua compilando** e passa a enxergar só a
primeira partição. A funcionalidade some sem sintoma.

Por isso o `CartaoSd.cpp` traz `static_assert(FF_MULTI_PARTITION == 1)`.
Verificado por teste negativo: forçando `0`, o build quebra.

## Consequências

* Uma dependência externa no build do firmware, que passa a exigir rede na
  primeira configuração do CMake.
* +89 KiB de flash e +3,7 KiB de RAM. Sobram 185,7 KiB de RAM.
* O `hw_config.cpp` é nosso e referencia o `Pinos.h`; acrescentar o display no
  mesmo barramento SPI (RNF06) vai exigir exclusão mútua e reconfiguração de
  velocidade por dispositivo, e o lugar disso já está identificado.

## Alternativas consideradas

* **Escrever o driver e o FAT.** Semanas para reimplementar algo com décadas
  de depuração, num projeto cujo assunto é outro.
* **`carlk3/no-OS-FatFS-SD-SPI-RPi-Pico`** (a anterior, só SPI, mais
  estrelas). A adotada é a sucessora mantida, e o SDIO que sobra não atrapalha.
