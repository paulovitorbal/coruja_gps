#include "led/LedRgbAnodoComum.h"

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>

namespace coruja {

namespace {
/// 8 bits de resolução: a `Cor` tem 8 bits por canal, então mais wrap não
/// acrescentaria nenhum passo distinguível.
constexpr std::uint16_t kWrap = 255;
}  // namespace

LedRgbAnodoComum::LedRgbAnodoComum(unsigned gpio_r, unsigned gpio_g,
                                   unsigned gpio_b)
    : gpio_r_(gpio_r), gpio_g_(gpio_g), gpio_b_(gpio_b) {
    configura_canal(gpio_r_);
    configura_canal(gpio_g_);
    configura_canal(gpio_b_);
    define_cor(cores::kApagado);
}

void LedRgbAnodoComum::configura_canal(unsigned gpio) {
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    const unsigned slice = pwm_gpio_to_slice_num(gpio);

    // O divisor sai do clock do sistema, não de uma constante: assim a
    // frequência do LED não muda se o clock for reconfigurado.
    const float div = static_cast<float>(clock_get_hz(clk_sys)) /
                      (static_cast<float>(kFrequenciaHz) * (kWrap + 1));
    pwm_set_clkdiv(slice, div);
    pwm_set_wrap(slice, kWrap);
    pwm_set_enabled(slice, true);
}

void LedRgbAnodoComum::escreve_canal(unsigned gpio, std::uint8_t intensidade) {
    // Ânodo comum: nível baixo acende, então o nível de PWM é o complemento da
    // intensidade. Intensidade 0 vira nível kWrap, que mantém o pino sempre
    // alto e o LED apagado.
    //
    // A inversão é aritmética e não por `pwm_set_output_polarity()` de
    // propósito: a polaridade em hardware é por *canal* de slice, e os três
    // GPIO deste LED se espalham por dois slices — GPIO 6 e 7 são os canais A e
    // B do mesmo slice, GPIO 8 é o canal A de outro. Acertar essa contabilidade
    // é fonte de bug silencioso; uma subtração não é.
    pwm_set_gpio_level(gpio, static_cast<std::uint16_t>(kWrap - intensidade));
}

void LedRgbAnodoComum::define_cor(const Cor& cor) {
    atual_ = cor;
    escreve_canal(gpio_r_, cor.r);
    escreve_canal(gpio_g_, cor.g);
    escreve_canal(gpio_b_, cor.b);
}

}  // namespace coruja
