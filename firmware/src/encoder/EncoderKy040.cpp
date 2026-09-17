#include "encoder/EncoderKy040.h"

#include <hardware/gpio.h>
#include <pico/time.h>

namespace coruja {

namespace {

void configura_entrada(unsigned gpio, bool pull_up) {
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_IN);
    if (pull_up) {
        gpio_pull_up(gpio);
    } else {
        gpio_disable_pulls(gpio);
    }
}

}  // namespace

EncoderKy040::EncoderKy040(bool pull_up_interno, bool invertido,
                           unsigned gpio_clk, unsigned gpio_dt,
                           unsigned gpio_sw)
    : gpio_clk_(gpio_clk),
      gpio_dt_(gpio_dt),
      gpio_sw_(gpio_sw),
      decodificador_(invertido) {
    configura_entrada(gpio_clk_, pull_up_interno);
    configura_entrada(gpio_dt_, pull_up_interno);
    configura_entrada(gpio_sw_, pull_up_interno);

    // Primeira amostra só para fixar a referência, sem gerar evento.
    decodificador_.amostra(gpio_get(gpio_clk_) != 0, gpio_get(gpio_dt_) != 0);
}

EventoEncoder EncoderKy040::proximo_evento() {
    const auto agora_ms = static_cast<std::uint32_t>(to_ms_since_boot(get_absolute_time()));

    // A chave fecha para GND: pino em nível baixo significa pressionado. A
    // inversão vive aqui para que o `AntiRepique` não precise conhecer a
    // polaridade do hardware.
    if (chave_.amostra(gpio_get(gpio_sw_) == 0, agora_ms)) {
        return EventoEncoder::Clique;
    }

    const int passo = decodificador_.amostra(gpio_get(gpio_clk_) != 0,
                                             gpio_get(gpio_dt_) != 0);
    if (passo > 0) {
        return EventoEncoder::GiroDireita;
    }
    if (passo < 0) {
        return EventoEncoder::GiroEsquerda;
    }
    return EventoEncoder::Nenhum;
}

}  // namespace coruja
