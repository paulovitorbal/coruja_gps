// Os tres simbolos que sobram quando o driver SDIO sai do build.
//
// 🔴 POR QUE: LICENCA. O driver SDIO da no-OS-FatFS-SD-SDIO-SPI-RPi-Pico e
// derivado do ZuluSCSI e e GPL-3.0-or-later; este projeto e PolyForm
// Noncommercial, e a GPL-3 proibe acrescentar restricao de uso nao
// comercial. O codigo foi tirado do build em
// `firmware/cmake/fatfs_sd_sem_sdio.cmake`. Ver docs/licencas/README.md.
//
// O caminho SPI da biblioteca -- que e o que este aparelho usa (ADR 0007) --
// ainda REFERENCIA estes tres simbolos, sempre atras de um teste de
// `sd_card_p->type`. Com o unico cartao declarado como `SD_IF_SPI`
// (`hw_config.cpp`), nenhum deles e alcancavel. Eles existem para o ligador,
// nao para rodar.
//
// ⚠️ Por isso NAO devolvem sucesso silencioso: se um dia alguem declarar um
// cartao `SD_IF_SDIO`, o aparelho tem de PARAR com a causa escrita, e nao
// montar um cartao que ninguem esta lendo de verdade. Base de radares
// silenciosamente errada e o pior desfecho possivel aqui -- o mesmo criterio
// da varredura de velocidade em `hw_config.cpp`.
//
// Em C mesmo, e nao em C++, porque quem chama e C e os nomes nao podem sofrer
// mangling.

#include <pico/stdlib.h>

#include "sd_card.h"

// Uma mensagem so, para as tres portas. `panic` escreve no stdio e trava o
// nucleo: no RP2350 isso e o fim de linha, que e exatamente o que se quer.
static void __attribute__((noreturn)) sem_driver_sdio(const char *quem) {
    panic("%s: o driver SDIO nao esta neste firmware (licenca GPL-3; ver "
          "docs/licencas). Este aparelho le o cartao por SPI.",
          quem);
}

// Chamado por `sd_driver/sd_card.c` no `case SD_IF_SDIO` da construcao.
void sd_sdio_ctor(sd_card_t *sd_card_p) {
    (void)sd_card_p;
    sem_driver_sdio("sd_sdio_ctor");
}

// Chamado por `sd_allocation_unit()`, que ja retorna antes quando o cartao e
// SPI. Chegar aqui significa cartao SDIO.
bool rp2040_sdio_get_sd_status(sd_card_t *sd_card_p, uint8_t response[64]) {
    (void)sd_card_p;
    (void)response;
    sem_driver_sdio("rp2040_sdio_get_sd_status");
}

// Chamado de dentro da interrupcao de DMA, no `if (SD_IF_SDIO == type)`.
// Mora na RAM como o original: interrupcao nao espera a flash.
void __not_in_flash_func(sdio_irq_handler)(sd_card_t *sd_card_p) {
    (void)sd_card_p;
    sem_driver_sdio("sdio_irq_handler");
}
