#pragma once

/// No alvo Pico isto configura o SPI e o driver de cartão. No host é um
/// interruptor do dublê: `FatFsFalso::driver_inicia` decide a resposta.
extern "C" bool sd_init_driver();
