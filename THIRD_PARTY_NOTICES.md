# Avisos de terceiros

O que vai **dentro do binário publicado** (`coruja_gps-vX.Y.Z.uf2`) e não é
deste projeto. Cada item mantém a sua própria licença: a licença do
coruja_gps (`LICENSE.md`) não vale para eles, nem a deles para o resto do
projeto.

Conferido em 19/11/2026 contra o mapa do ligador
(`firmware/build-pico/coruja_gps.elf.map`) do build com `PICO_BOARD=pico2_w`,
`pico-sdk` 2.3.1 — ou seja, a lista é do que o ligador de fato pôs na imagem,
e não do que o `CMakeLists.txt` menciona. Os textos das licenças estão onde a
tabela indica; o que não está versionado aqui vem junto com a dependência.

| Componente | Licença | Texto |
| :--- | :--- | :--- |
| Raspberry Pi Pico SDK | BSD-3-Clause | `pico-sdk/LICENSE.TXT` |
| lwIP | BSD-3-Clause | `pico-sdk/lib/lwip/COPYING` |
| Mbed TLS | Apache-2.0 **ou** GPL-2.0-or-later (dupla) | `pico-sdk/lib/mbedtls/LICENSE` |
| TinyUSB | MIT | `pico-sdk/lib/tinyusb/LICENSE` |
| cyw43-driver | **licença própria, não é BSD** — ver abaixo | `pico-sdk/lib/cyw43-driver/LICENSE` e `LICENSE.RP` |
| firmware do CYW43 (blobs) | distribuído com o cyw43-driver | `pico-sdk/lib/cyw43-driver/firmware/` |
| no-OS-FatFS-SD-SDIO-SPI-RPi-Pico | Apache-2.0 | `LICENSE` do repositório |
| FatFs (ChaN) | licença própria, de duas cláusulas | `src/ff15/LICENSE.txt` daquele repositório |
| driver SDIO (derivado do ZuluSCSI) | **GPL-3.0-or-later** — ver abaixo | `src/ZuluSCSI-firmware/LICENSE` daquele repositório |
| driver SPI (derivado do Mbed OS) | Apache-2.0 | cabeçalho de `sd_driver/SPI/sd_card_spi.c` |
| JetBrains Mono (bitmaps) | SIL Open Font License 1.1 | `docs/licencas/OFL.txt` |
| Twemoji (sprites) | CC-BY 4.0 | `docs/licencas/CC-BY-4.0-twemoji.txt` |

As ferramentas de build — `picotool`, `pioasm`, GoogleTest, o toolchain da Arm
— **não** entram na imagem e por isso não estão na tabela.

## cyw43-driver: a licença que não é BSD

O rádio Wi-Fi do Pico 2 W é dirigido pelo `cyw43-driver`, de **George
Robotics Pty Ltd**. Ele vem no SDK com **duas** licenças, e nenhuma é BSD:

- `LICENSE` — texto no estilo BSD, mas com uma terceira cláusula que muda
  tudo: *"Any redistribution, use, or modification in source or binary form
  is done solely for personal benefit and not for any commercial purpose or
  for monetary gain."* É uso **não comercial**, e o próprio README do driver
  resume: *"This cyw43-driver is free only for non-commercial use"*.
- `LICENSE.RP` — alternativa da Raspberry Pi, **sem** a restrição comercial,
  mas limitada ao silício dela: *"use this software solely with the Licensor's
  microcontroller chip (RP2040) or any other semiconductor device produced by
  the Licensor"*.

Este projeto roda num RP2350 da Raspberry Pi, então o `LICENSE.RP` se aplica.
A consequência prática é que **o binário publicado não é portável para outro
silício**: levar este firmware para um rádio CYW43 fora de uma peça da
Raspberry Pi recairia na outra licença, a não comercial.

O licenciante oferece termos comerciais por contato próprio — ver o fim do
`LICENSE` do driver.

## O driver SDIO e a GPL-3

A biblioteca de cartão SD traz **dois** drivers: o SPI, que é o que este
aparelho usa (ver `docs/adr/0007`), e um SDIO de 4 bits. O `CMakeLists.txt`
dela compila os dois, e o mapa do ligador mostra o SDIO **dentro da imagem**
(`rp2040_sdio.c.o` em `0x1000e440`), mesmo sem ninguém chamá-lo.

O README da biblioteca diz que esse driver é derivado do
[ZuluSCSI-firmware](https://github.com/ZuluSCSI/ZuluSCSI-firmware), e ela
versiona o `LICENSE` dele junto: **GPL-3.0-or-later**. Os arquivos
`sd_driver/SDIO/*.c` não trazem cabeçalho de licença próprio.

⚠️ **Isto é um ponto em aberto, e a decisão é do autor.** Código sob GPL-3
dentro de um binário publicado traz as obrigações da GPL-3 para esse binário,
e elas não convivem com uma licença não comercial. Os caminhos possíveis, sem
que nenhum seja recomendação jurídica:

1. deixar de compilar o SDIO (a biblioteca não tem opção para isso hoje; daria
   um `CMakeLists.txt` próprio para os fontes que o projeto usa);
2. tratar a questão com quem publica a biblioteca;
3. publicar o binário sob termos compatíveis com a GPL-3.

Enquanto não houver decisão, o fato fica registrado aqui.

## JetBrains Mono

Os cabeçalhos `firmware/src/display/Fonte*.h` são **mapas de bits
rasterizados** da JetBrains Mono SemiBold, gerados por
`scripts/gera_fonte.py`. Mapa de bits derivado de fonte continua sob a OFL
1.1, que exige o aviso de copyright junto do derivado — ele está no topo de
cada cabeçalho gerado, e o texto da licença em `docs/licencas/OFL.txt`.

> Copyright 2020 The JetBrains Mono Project Authors
> (https://github.com/JetBrains/JetBrainsMono)

O `.ttf` não está no repositório: o gerador o lê de `~/Library/Fonts`.

## Twemoji

`firmware/src/display/Sprites.h` traz dois ícones de 40×40 em RGB565, gerados
por `scripts/gera_sprites.py` a partir da arte do **Twemoji**, © Twitter Inc.
e colaboradores, sob CC-BY 4.0. A atribuição está no cabeçalho do arquivo
gerado e os PNG de origem em `ativos/sprites/`.

## Certificados de raiz

`firmware/src/rede/RaizesConfiaveis.h` embute três certificados de raiz
públicos (ISRG Root X1, ISRG Root X2 e GTS Root R4), gerados por
`scripts/gera_raizes.py`. São documentos públicos das respectivas
autoridades, publicados para serem distribuídos com software, e não software
licenciado.
