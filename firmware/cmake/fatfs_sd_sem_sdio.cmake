# Declara o alvo da no-OS-FatFS-SD-SDIO-SPI-RPi-Pico **sem o driver SDIO**.
#
# 🔴 POR QUE ESTE ARQUIVO EXISTE: LICENCA, NAO GOSTO.
#
# A biblioteca e Apache-2.0, mas o driver SDIO dela (`sd_driver/SDIO/`) e
# derivado do ZuluSCSI e e **GPL-3.0-or-later**. Este projeto e licenciado
# sob PolyForm Noncommercial, e a GPL-3 proibe acrescentar restricao de uso
# nao comercial: publicar o `.uf2` com aquele codigo dentro seria violar a
# GPL. O `CMakeLists.txt` da biblioteca compila os dois drivers
# incondicionalmente e nao tem opcao para desligar o SDIO.
#
# O aparelho usa SPI (ver docs/adr/0007). O SDIO nunca foi executado aqui --
# era peso morto que so trazia um problema juridico.
#
# Esta e a lista de fontes do `src/CMakeLists.txt` da v3.6.2 **menos**
# `sd_driver/SDIO/rp2040_sdio.c` e `sd_driver/SDIO/sd_card_sdio.c`, e sem o
# `pico_generate_pio_header` de `rp2040_sdio.pio` (que so aquele driver usa).
# Os tres simbolos que o caminho SPI ainda referencia vem de
# `src/armazenamento/sdio_ausente.c`, deste projeto.
#
# ⚠️ **Ao subir a versao da biblioteca, reconfira esta lista** contra o
# `src/CMakeLists.txt` dela: fonte nova la nao aparece aqui sozinha, e o
# sintoma e erro de link, nao funcionalidade sumindo em silencio.

set(CORUJA_FATFS_SD_DIR ${no-os-fatfs-sd-sdio-spi-rpi-pico_SOURCE_DIR}/src)

add_library(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico INTERFACE)

target_compile_definitions(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico INTERFACE
    PICO_MAX_SHARED_IRQ_HANDLERS=8u
)

target_sources(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico INTERFACE
    ${CORUJA_FATFS_SD_DIR}/ff15/source/ff.c
    ${CORUJA_FATFS_SD_DIR}/ff15/source/ffsystem.c
    ${CORUJA_FATFS_SD_DIR}/ff15/source/ffunicode.c
    ${CORUJA_FATFS_SD_DIR}/sd_driver/dma_interrupts.c
    ${CORUJA_FATFS_SD_DIR}/sd_driver/sd_card.c
    ${CORUJA_FATFS_SD_DIR}/sd_driver/sd_timeouts.c
    # sd_driver/SDIO/rp2040_sdio.c   -- FORA: GPL-3 (ZuluSCSI)
    # sd_driver/SDIO/sd_card_sdio.c  -- FORA: GPL-3 (ZuluSCSI)
    ${CORUJA_FATFS_SD_DIR}/sd_driver/SPI/my_spi.c
    ${CORUJA_FATFS_SD_DIR}/sd_driver/SPI/sd_card_spi.c
    ${CORUJA_FATFS_SD_DIR}/sd_driver/SPI/sd_spi.c
    ${CORUJA_FATFS_SD_DIR}/src/crash.c
    ${CORUJA_FATFS_SD_DIR}/src/crc.c
    ${CORUJA_FATFS_SD_DIR}/src/f_util.c
    ${CORUJA_FATFS_SD_DIR}/src/ff_stdio.c
    ${CORUJA_FATFS_SD_DIR}/src/file_stream.c
    ${CORUJA_FATFS_SD_DIR}/src/glue.c
    ${CORUJA_FATFS_SD_DIR}/src/my_debug.c
    ${CORUJA_FATFS_SD_DIR}/src/my_rtc.c
    ${CORUJA_FATFS_SD_DIR}/src/util.c
)

target_include_directories(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico INTERFACE
    ${CORUJA_FATFS_SD_DIR}/ff15/source
    ${CORUJA_FATFS_SD_DIR}/sd_driver
    ${CORUJA_FATFS_SD_DIR}/include
)

# `hardware_pio` continua na lista mesmo sem o driver SDIO: `sd_card.h` inclui
# `SDIO/rp2040_sdio.h`, que inclui `hardware/pio.h` pelos TIPOS do
# `sd_sdio_if_t`. Cabecalho nao vira codigo -- nenhum byte de PIO entra.
target_link_libraries(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico INTERFACE
    hardware_dma
    hardware_pio
    hardware_spi
    pico_aon_timer
    pico_stdlib
    cmsis_core
)
