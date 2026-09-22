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

/// 12 MHz — **escolhido por medição**, não copiado do exemplo da biblioteca.
///
/// A varredura de 2026-09-22 leu 128 KiB de setores por velocidade, em cinco
/// inicializações, comparando o CRC-32 contra a leitura mais lenta:
///
///   1, 2, 4, 6, 8, 10, 12, 16 MHz e 18,75 MHz ... CRC idêntico em todas
///
/// Foram 6,4 MB sem uma divergência. O teto é 18,75 MHz, imposto pelo divisor
/// do RP2350 — pedir 20 ou 24 devolve o mesmo valor.
///
/// 12 MHz fica a dois degraus do teto medido, o que dá margem sem penalizar:
/// os 214 KB da base saem em ~0,15 s.
///
/// ⚠️ **Este teto é da PROTOBOARD, não do projeto.** Fio sem plano de terra e
/// ponta sem terminação é outro barramento; na perfboard, remeça.
///
/// Histórico que vale conhecer antes de mexer: este valor esteve em 1 MHz por
/// algumas horas, baixado a partir de uma falha que eu atribuí a velocidade e
/// que era outra coisa — a protoboard não estava entregando 3,3 V. O critério
/// da varredura não é "montou", é **CRC igual**: em velocidade marginal o
/// cartão devolve dados corrompidos sem erro nenhum, e uma base de radares
/// silenciosamente errada é o pior desfecho possível aqui.
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
