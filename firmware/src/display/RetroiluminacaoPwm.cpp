#include "display/RetroiluminacaoPwm.h"

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>

#include "display/Brilho.h"
#include "placa/Pinos.h"

namespace coruja {

namespace {

/// Mantem os 16 bits do `Brilho`: o wrap e o maximo que cabe em 16 bits.
constexpr std::uint16_t kWrap = 65535;

/// Frequencia efetiva = clk_sys / (divisor * (wrap + 1)).
///
/// Com 150 MHz e divisor 1, sao ~2,3 kHz -- **abaixo dos 20 kHz do RF04**.
/// Para chegar la com 16 bits de resolucao seria preciso clk_sys de 1,3 GHz,
/// que o chip nao tem. A saida e reduzir a resolucao do PWM, nao a
/// frequencia: 8 bits dao ~586 kHz, folgado acima do limiar, e 256 niveis
/// sobre uma curva de 20 passos ainda deixam 12 niveis entre passos
/// vizinhos -- mais do que o olho separa.
constexpr std::uint16_t kWrap8 = 255;

constexpr std::uint32_t frequencia_hz(std::uint32_t clk_sys_hz,
                                      std::uint16_t wrap) {
    return clk_sys_hz / (static_cast<std::uint32_t>(wrap) + 1U);
}

// O numero do RP2350 em repouso. Se o clock do sistema mudar, esta conta
// muda junto, e o assert e o que avisa antes de o painel cintilar no carro.
constexpr std::uint32_t kClkSysEsperadoHz = 150000000;

static_assert(frequencia_hz(kClkSysEsperadoHz, kWrap8) >= kFrequenciaPwmHz,
              "PWM do backlight abaixo dos 20 kHz do RF04");
static_assert(frequencia_hz(kClkSysEsperadoHz, kWrap) < kFrequenciaPwmHz,
              "se 16 bits ja passam de 20 kHz, use kWrap e ganhe resolucao");

}  // namespace

RetroiluminacaoPwm::RetroiluminacaoPwm() {
    gpio_set_function(pinos::kDisplayBacklight, GPIO_FUNC_PWM);
    const unsigned fatia = pwm_gpio_to_slice_num(pinos::kDisplayBacklight);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 1.0F);
    pwm_config_set_wrap(&cfg, kWrap8);
    pwm_init(fatia, &cfg, true);
    define_duty(0);
}

void RetroiluminacaoPwm::define_duty(std::uint16_t duty) {
    duty_ = duty;
    // Os 16 bits do Brilho descem para os 8 do PWM. Divisao por 257 e nao
    // deslocamento de 8: 65535 tem de virar 255 exatos, senao o topo da
    // escala nunca alcanca o brilho maximo.
    const std::uint16_t nivel = static_cast<std::uint16_t>(duty / 257U);
    pwm_set_gpio_level(pinos::kDisplayBacklight, nivel);
}

}  // namespace coruja
