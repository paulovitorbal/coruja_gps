#include "led/LedRgbPwm.h"

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>

namespace coruja {

namespace {
/// 8 bits de resolução: a `Cor` tem 8 bits por canal, então mais wrap não
/// acrescentaria nenhum passo distinguível.
constexpr std::uint16_t kWrap = 255;
}  // namespace

LedRgbPwm::LedRgbPwm(unsigned gpio_r, unsigned gpio_g, unsigned gpio_b)
    : gpio_r_(gpio_r), gpio_g_(gpio_g), gpio_b_(gpio_b) {
    configura_canal(gpio_r_);
    configura_canal(gpio_g_);
    configura_canal(gpio_b_);
    define_cor(cores::kApagado);
}

void LedRgbPwm::configura_canal(unsigned gpio) {
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

void LedRgbPwm::escreve_canal(unsigned gpio, std::uint8_t intensidade) {
    pwm_set_gpio_level(gpio, intensidade);
}

void LedRgbPwm::define_cor(const Cor& cor) {
    atual_ = cor;
    escreve_canal(gpio_r_, cor.r);
    escreve_canal(gpio_g_, cor.g);
    escreve_canal(gpio_b_, cor.b);
}

}  // namespace coruja
