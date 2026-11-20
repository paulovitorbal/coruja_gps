# Avisos de terceiros

Este arquivo vai **anexo a cada release**, junto do `coruja_gps-vX.Y.Z.uf2`.
Quem baixa só o binário não passa pelo repositório, e as licenças de dentro
dele pedem que o aviso acompanhe a redistribuição binária.

O detalhe — por dependência, com o que cada licença exige neste uso concreto —
está em **`docs/licencas/README.md`**, que é onde este projeto guarda isso. Aqui
fica só a lista, para o arquivo valer sozinho.

O binário embute, além do código deste projeto:

| Componente | Licença |
| :--- | :--- |
| Raspberry Pi Pico SDK | BSD-3-Clause |
| lwIP | BSD-3-Clause |
| Mbed TLS | Apache-2.0 (dupla com GPL-2.0-or-later; tomamos a Apache) |
| TinyUSB | MIT |
| cyw43-driver (George Robotics) | própria, **não é BSD** — ver abaixo |
| no-OS-FatFS-SD-SDIO-SPI-RPi-Pico | Apache-2.0 |
| FatFs (ChaN) | própria, estilo BSD de duas cláusulas |
| driver SPI, derivado do Mbed OS | Apache-2.0 |
| JetBrains Mono (bitmaps dos `Fonte*.h`) | SIL Open Font License 1.1 |
| Twemoji (sprites do `Sprites.h`) | CC-BY 4.0 |

Cada um mantém a sua licença: a do coruja_gps (`LICENSE.md`) não vale para
eles, nem a deles para o resto do projeto.

**Dois pontos que mudam o que se pode fazer com o binário:**

- **cyw43-driver não é BSD.** A licença padrão dele é só para uso não
  comercial; a alternativa da Raspberry Pi (`LICENSE.RP`) tira essa restrição
  mas vale apenas com silício da Raspberry Pi. Este firmware roda num RP2350,
  então é a `LICENSE.RP` que se aplica — e o binário **não** é portável para um
  rádio CYW43 fora de uma peça da Raspberry Pi.
- **O driver SDIO ficou de fora, de propósito.** A biblioteca de cartão SD traz
  um driver SDIO derivado do ZuluSCSI-firmware, sob GPL-3.0-or-later, que o
  `CMakeLists.txt` dela compila mesmo com o projeto usando SPI. Ele **não entra
  nesta imagem**: o build do Coruja declara o alvo da biblioteca sem aqueles
  dois fontes. Por isso a GPL-3 não alcança este binário. Como e por quê, no
  `docs/licencas/README.md`.

Os **textos** das licenças — o que BSD-3, MIT e Apache-2.0 pedem junto do
binário — vão anexos à mesma release, no `THIRD_PARTY_LICENSES.txt`, copiados
dos arquivos que o build usou. As bibliotecas não estão no repositório, então
quem baixa só a release não as teria de outro modo. Os da JetBrains Mono e do
Twemoji também estão versionados em `docs/licencas/`.
