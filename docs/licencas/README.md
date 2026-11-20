# Licenças

As licenças que este projeto precisa guardar junto do código: a **do próprio
projeto**, na parte de documentação e hardware, e as **de terceiros** cujo
software vai dentro do binário ou dos arquivos gerados.

A regra deste diretório é a de sempre: a licença está aqui porque a
distribuição é real, e cada seção diz **o que ela exige neste uso concreto** —
não o resumo genérico da licença.

A licença do código do projeto é outra, e fica na raiz: `LICENSE.md`
(PolyForm Noncommercial 1.0.0), com o `NOTICE` e o `PERMISSOES-ADICIONAIS.md`
ao lado.

## CC BY-NC 4.0 — a documentação e o hardware DESTE projeto

`CC-BY-NC-4.0.txt` é o *legalcode* oficial da Creative Commons
Atribuição-NãoComercial 4.0 Internacional, baixado de
<https://creativecommons.org/licenses/by-nc/4.0/legalcode.txt>. **Não é
licença de terceiro:** é a licença que o autor escolheu para `docs/`,
`kicad/`, `caixa/`, o `.fzz` e os documentos de projeto da raiz. Quais
caminhos ficam sob ela está no `README.md` da raiz.

Ela está aqui, e não num diretório próprio, porque é o lugar onde este
projeto guarda texto de licença.

# Licenças de terceiros

As duas primeiras seções são de arte e tipografia, que viram **arquivos
gerados** versionados aqui. As demais são das **bibliotecas** que o ligador
põe dentro do `coruja_gps.uf2`: elas não estão no repositório, e o texto de
cada licença vem com a dependência, no caminho indicado.

## JetBrains Mono — `OFL.txt`

O firmware embute **mapas de bits rasterizados** da JetBrains Mono SemiBold,
gerados por `scripts/gera_fonte.py` e versionados em
`firmware/src/display/FonteNumero.h` e `FonteTexto.h`.

**Por que a licença está aqui.** A SIL Open Font License 1.1 exige que o aviso
de copyright e o texto da licença acompanhem a fonte **e seus derivados**. Um
mapa de bits rasterizado é derivado da fonte, e este repositório é público,
então a distribuição é real e não hipotética.

O arquivo `.ttf` **não** está no repositório: o gerador o lê de
`~/Library/Fonts`. Quem for regerar precisa instalar a fonte, disponível em
<https://www.jetbrains.com/lp/mono/>.

**O que a OFL permite aqui, sem ambiguidade:** usar, embutir e redistribuir,
inclusive em produto, inclusive rasterizado. O que ela veda é vender a fonte
isolada e usar os Nomes Reservados numa versão modificada — nada disso se
aplica a este uso.

## Twemoji — `CC-BY-4.0-twemoji.txt`

O firmware embute dois ícones de 40×40 em RGB565, gerados por
`scripts/gera_sprites.py` de arte do **Twemoji** e versionados em
`firmware/src/display/Sprites.h`:

| Ícone | Codepoint | Uso |
| :--- | :--- | :--- |
| 🏎 | `U+1F3CE` | radar, fixo e móvel (§4.1) |
| 🚦 | `U+1F6A6` | semáforo, e semáforo com radar |

Os PNG de origem (72×72) estão em `ativos/sprites/`, versionados de
propósito: sem eles a geração não é reproduzível, e uma URL não é garantia
de que o arquivo continue lá.

**Atribuição, que é o que a CC-BY 4.0 exige:** arte do Twemoji,
© Twitter Inc. e colaboradores, licenciada sob CC-BY 4.0. Está no cabeçalho
do `Sprites.h` gerado, e não só aqui — quem ler só o código também vê.

## O que vem dentro do binário publicado

Esta lista saiu do **mapa do ligador** (`firmware/build-pico/coruja_gps.elf.map`,
build de 19/11/2026 com `PICO_BOARD=pico2_w` e `pico-sdk` 2.3.1), e não do que
o `CMakeLists.txt` menciona: é o que o ligador de fato pôs na imagem.

Nenhuma dessas bibliotecas está versionada aqui — elas chegam pelo SDK ou pelo
`FetchContent`. O texto de cada licença vem com elas, no caminho da tabela.

| Componente | Licença | Texto, onde ele vem |
| :--- | :--- | :--- |
| Raspberry Pi Pico SDK | BSD-3-Clause | `pico-sdk/LICENSE.TXT` |
| lwIP | BSD-3-Clause | `pico-sdk/lib/lwip/COPYING` |
| Mbed TLS | Apache-2.0 **ou** GPL-2.0-or-later, à escolha de quem usa | `pico-sdk/lib/mbedtls/LICENSE` |
| TinyUSB | MIT | `pico-sdk/lib/tinyusb/LICENSE` |
| cyw43-driver | própria, **não é BSD** — ver abaixo | `pico-sdk/lib/cyw43-driver/LICENSE` e `LICENSE.RP` |
| no-OS-FatFS-SD-SDIO-SPI-RPi-Pico | Apache-2.0 | `LICENSE` do repositório |
| FatFs (ChaN) | própria, de duas cláusulas, estilo BSD | `src/ff15/LICENSE.txt`, dentro da anterior |
| driver SDIO (do ZuluSCSI) | **GPL-3.0-or-later** — ver abaixo | `src/ZuluSCSI-firmware/LICENSE`, dentro da anterior |
| driver SPI (do Mbed OS) | Apache-2.0 | cabeçalho de `sd_driver/SPI/sd_card_spi.c` |

O `picotool`, o `pioasm`, o GoogleTest e o toolchain da Arm **não** entram na
imagem: são ferramentas de build, e por isso não estão aqui.

**O que as três primeiras exigem neste uso:** as licenças BSD-3-Clause e a MIT
pedem que o aviso de copyright e o texto da licença acompanhem a redistribuição
**em forma binária**, na documentação ou nos materiais que vão junto. Um
`.uf2` sozinho numa página de release não cumpre isso — é por isso que o
`scripts/libera_fw.sh` anexa o `THIRD_PARTY_NOTICES.md`, que aponta para este
arquivo, junto do binário. Do Mbed TLS, que é dupla, este projeto toma a
**Apache-2.0**: a GPL-2.0 não conviveria com a licença não comercial do
projeto.

### cyw43-driver: a licença que não é BSD

O rádio Wi-Fi do Pico 2 W é dirigido pelo `cyw43-driver`, de **George Robotics
Pty Ltd**, que vem no SDK com **duas** licenças, e nenhuma é BSD:

- `LICENSE` — parece BSD, mas a terceira cláusula muda tudo: *"Any
  redistribution, use, or modification in source or binary form is done solely
  for personal benefit and not for any commercial purpose or for monetary
  gain."* O README do próprio driver resume: *"This cyw43-driver is free only
  for non-commercial use"*.
- `LICENSE.RP` — alternativa da Raspberry Pi, **sem** a restrição comercial,
  mas amarrada ao silício dela: *"use this software solely with the Licensor's
  microcontroller chip (RP2040) or any other semiconductor device produced by
  the Licensor"*.

**O que isso exige aqui:** o aparelho é um RP2350 da Raspberry Pi, então vale
a `LICENSE.RP`, e as duas pedem o aviso de copyright junto da redistribuição
binária. A consequência prática é que **o binário não é portável para outro
silício**: levar este firmware para um rádio CYW43 que não esteja numa peça da
Raspberry Pi cairia na outra licença, a não comercial. Para quem quiser termos
comerciais, o fim do `LICENSE` do driver traz o contato do licenciante.

Vale notar que a licença não comercial do driver e a escolha de uma licença não
comercial para este projeto são **coincidência**, e não causa: são licenciantes
diferentes, e cada uma vale por si.

### O driver SDIO e a GPL-3 — ponto em aberto

A biblioteca de cartão SD traz **dois** drivers: o SPI, que é o que este
aparelho usa (ver `docs/adr/0007`), e um SDIO de 4 bits. O `CMakeLists.txt`
dela compila os dois, e o mapa mostra o SDIO **dentro da imagem**
(`rp2040_sdio.c.o` em `0x1000e440`) mesmo sem ninguém chamá-lo.

O README da biblioteca diz que esse driver é derivado do
[ZuluSCSI-firmware](https://github.com/ZuluSCSI/ZuluSCSI-firmware), e ela
versiona o `LICENSE` dele junto: **GPL-3.0-or-later**. Os arquivos
`sd_driver/SDIO/*.c` não trazem cabeçalho de licença próprio.

⚠️ **A decisão é do autor, e ainda não foi tomada.** Código sob GPL-3 dentro
de um binário publicado traz as obrigações da GPL-3 para esse binário, e elas
não convivem com uma licença não comercial. Três caminhos, nenhum deles
recomendação jurídica:

1. não compilar o SDIO — a biblioteca não tem opção para isso hoje, e daria um
   `CMakeLists.txt` próprio listando só os fontes que o projeto usa;
2. tratar a questão com quem publica a biblioteca;
3. publicar o binário sob termos compatíveis com a GPL-3.

Enquanto não houver decisão, o fato fica registrado aqui — e **não** some do
`THIRD_PARTY_NOTICES.md` que vai na release.

## Certificados de raiz

`firmware/src/rede/RaizesConfiaveis.h` embute três certificados de raiz
públicos (ISRG Root X1, ISRG Root X2 e GTS Root R4), gerados por
`scripts/gera_raizes.py`. São documentos públicos das autoridades
certificadoras, publicados para serem distribuídos com software, e não
software licenciado — por isso não têm seção de licença aqui.
