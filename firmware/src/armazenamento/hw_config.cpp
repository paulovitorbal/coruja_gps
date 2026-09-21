// Configuração de hardware exigida pela no-OS-FatFS-SD-SDIO-SPI-RPi-Pico.
//
// A biblioteca não descobre a fiação: ela **declara** `sd_get_num()` e
// `sd_get_by_num()` e cabe à aplicação implementá-las. É por isso que este
// arquivo existe, e por isso ele é `.cpp` e não `.c` — assim os pinos saem do
// `Pinos.h`, que é a fonte da verdade do projeto, em vez de virarem uma
// segunda cópia livre para divergir.
#include <hardware/spi.h>

#include "placa/Pinos.h"

extern "C" {
#include "hw_config.h"
}

namespace {

/// 12 MHz é o valor do exemplo da própria biblioteca e é conservador para fio
/// de protoboard, onde o barramento SPI não tem terminação nenhuma. A
/// inicialização do cartão acontece a 400 kHz; quem cuida disso é o driver.
///
/// ⚠️ O barramento é **compartilhado com o display** (RNF06). Quando o driver
/// do display entrar, a velocidade terá de ser reconfigurada por dispositivo,
/// antes de cada transação, e as duas metades precisarão de exclusão mútua.
constexpr unsigned kBaudRateHz = 12 * 1000 * 1000;

spi_t g_spi = {
    .hw_inst   = spi0,
    .miso_gpio = coruja::pinos::kSpiMiso,
    .mosi_gpio = coruja::pinos::kSpiMosi,
    .sck_gpio   = coruja::pinos::kSpiSck,
    .baud_rate = kBaudRateHz,
};

sd_spi_if_t g_interface = {
    .spi    = &g_spi,
    .ss_gpio = coruja::pinos::kSdCs,
};

sd_card_t g_cartao = {
    .type     = SD_IF_SPI,
    .spi_if_p = &g_interface,

    // **Cartão inserido = nível ALTO.** MEDIDO sob os três pulls internos, nos
    // dois estados, em 2026-09-20:
    //
    //   cartão dentro .... ALTO nos três  -> acionado em alto (pull-up)
    //   slot vazio ....... BAIXO nos três -> acionado em baixo (chave ao GND)
    //
    // Coincide com o que a Adafruit documenta. Mas o caminho até aqui passou
    // por uma inversão errada desta constante (R-41): duas leituras isoladas,
    // feitas com pull-down num pino que na época estava FLUTUANDO, mediram a
    // configuração do firmware em vez do cartão — e pareciam conclusivas.
    //
    // Se algum dia esta linha precisar mudar, meça sob os três pulls antes.
    // `CartaoSd::diagnostica_det()` existe exatamente para isso, e roda a cada
    // clique. Uma leitura só não distingue pino flutuante de pino acionado.
    .use_card_detect = true,
    .card_detect_gpio = coruja::pinos::kSdDet,
    .card_detected_true = 1,

    // Pull-DOWN interno, coerente com a polaridade acima: se o módulo estiver
    // desconectado, o pino lê BAIXO, que aqui significa "sem cartão" — a
    // resposta conservadora. Com o módulo ligado ele é irrelevante, perdendo
    // de longe para o pull-up da placa e para a chave ao GND.
    .card_detect_use_pull = true,
    .card_detect_pull_hi = false,
};

}  // namespace

extern "C" size_t sd_get_num() { return 1; }

extern "C" sd_card_t* sd_get_by_num(size_t num) {
    return num == 0 ? &g_cartao : nullptr;
}
