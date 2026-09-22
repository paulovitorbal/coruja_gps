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

/// 1 MHz — **baixado de 12 MHz em 2026-09-22**, por medição.
///
/// A 12 MHz um cartão de 32 GB inicializava e depois falhava em toda leitura
/// de setor com `FR_DISK_ERR`, enquanto outro cartão no mesmo soquete
/// funcionava. A assimetria é a pista: a inicialização roda a 400 kHz e
/// passava; a leitura roda neste valor e não passava.
///
/// 12 MHz era o número do exemplo da biblioteca, e o comentário anterior o
/// chamava de "conservador para fio de protoboard" — não era. Protoboard não
/// tem plano de terra e cada jumper é uma ponta sem terminação.
///
/// A conta que justifica não ter pressa: a base tem 214 KB e é lida uma vez
/// no boot. A 1 MHz são ~1,8 s; a 12 MHz, ~0,15 s. O tempo de boot não é o
/// gargalo deste aparelho, e leitura que falha custa infinitamente mais que
/// leitura lenta.
///
/// ⚠️ O barramento é **compartilhado com o display** (RNF06). Quando o driver
/// do display entrar, a velocidade terá de ser reconfigurada por dispositivo,
/// antes de cada transação, e as duas metades precisarão de exclusão mútua.
constexpr unsigned kBaudRateHz = 1 * 1000 * 1000;

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

    // **Sem card detect.** Removido em 2026-09-22 (ADR 0010): o projeto reage
    // igual a cartão ausente e a cartão ilegível, então a distinção não pagava
    // o GPIO nem o fio.
    //
    // E a chave do soquete não era confiável: com o cartão inserido ela não
    // abria por completo, deixando ~2,5 kΩ para o GND. Contra o pull-up de
    // 4,82 kΩ isso põe o pino em 1,13 V — medido —, dentro da zona
    // indeterminada da lógica de 3,3 V. O firmware lia ora alto, ora baixo,
    // com o mesmo código, e o pino existia para *aumentar* a confiança no
    // diagnóstico.
    .use_card_detect = false,
};

}  // namespace

extern "C" size_t sd_get_num() { return 1; }

extern "C" sd_card_t* sd_get_by_num(size_t num) {
    return num == 0 ? &g_cartao : nullptr;
}
