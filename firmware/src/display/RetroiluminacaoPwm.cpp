#include "display/RetroiluminacaoPwm.h"

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>

#include "display/Brilho.h"
#include "placa/Pinos.h"

namespace coruja {

namespace {

/// **Wrap de 7499, que da exatamente 20,0 kHz com divisor 1.**
///
/// A escolha e do fundo da curva, nao do topo. A primeira versao usava
/// wrap de 8 bits (255) porque 16 bits a 150 MHz dariam 2,3 kHz, abaixo
/// dos 20 kHz que o RF04 exige. O raciocinio estava certo na frequencia e
/// **errado na resolucao**: com 256 niveis, `duty/257` manda os passos de
/// 5% e 10% para os niveis 0 e 1 -- a tela **apaga** no piso do RF04.
///
/// O comentario original justificava os 8 bits dizendo que "256 niveis
/// sobre 20 passos deixam 12 niveis entre passos vizinhos". Isso vale para
/// uma escala linear; a curva e `(pct/100)^2,2`, e no fundo dela os passos
/// distam 1 ou 2 niveis. O erro foi raciocinar como linear exatamente na
/// regiao que a curva existe para tratar.
///
/// Por que 20,0 kHz e nao mais: menor frequencia = periodo maior = pulso
/// mais longo para a mesma fracao de duty, e no piso da curva o pulso e o
/// recurso escasso. A 20 kHz, 5% de brilho sao 10/7500 de 50 us = 67 ns;
/// a 36,6 kHz seriam 40 ns. O RF04 pede >= 20 kHz, entao 20,0 e o melhor
/// lugar da faixa para o fundo da escala.
constexpr std::uint16_t kWrap = 7499;
constexpr std::uint16_t kNiveis = kWrap + 1;

/// O numero do RP2350 em repouso. Se o clock do sistema mudar, esta conta
/// muda junto, e o assert e o que avisa antes de o painel cintilar no
/// carro ou assobiar na gaveta.
constexpr std::uint32_t kClkSysEsperadoHz = 150000000;

constexpr std::uint32_t frequencia_hz(std::uint32_t clk_sys_hz,
                                      std::uint32_t niveis) {
    return clk_sys_hz / niveis;
}

constexpr std::uint16_t nivel_de(std::uint16_t duty) {
    // Arredonda, nao trunca: no fundo da curva a diferenca entre 5,6 e 5 e
    // um degrau inteiro de brilho percebido.
    const std::uint32_t n =
        (static_cast<std::uint32_t>(duty) * kNiveis + 32768U) / 65536U;
    // **Duty diferente de zero nunca vira nivel zero.** E a promessa que
    // `Brilho::duty()` faz a quem chama, e a porta tem de honra-la: com o
    // visor apagado o usuario perde a referencia para recupera-lo.
    if (duty > 0 && n == 0) {
        return 1;
    }
    return static_cast<std::uint16_t>(n);
}

static_assert(frequencia_hz(kClkSysEsperadoHz, kNiveis) >= kFrequenciaPwmHz,
              "PWM do backlight abaixo dos 20 kHz do RF04");

// A guarda que faltava na primeira versao, e que teria pego o defeito
// antes de ele chegar na bancada: o piso da curva tem de acender.
static_assert(nivel_de(kDutyMinimo) > 0,
              "o primeiro passo da curva apaga a tela nesta resolucao");
static_assert(nivel_de(kDutyMinimo) >= 4,
              "resolucao insuficiente no fundo da curva: os primeiros "
              "passos ficariam indistinguiveis entre si");
static_assert(nivel_de(0) == 0, "duty zero tem de apagar de fato");
static_assert(nivel_de(65535) == kNiveis, "duty cheio tem de saturar");

}  // namespace

RetroiluminacaoPwm::RetroiluminacaoPwm() {
    gpio_set_function(pinos::kDisplayBacklight, GPIO_FUNC_PWM);
    const unsigned fatia = pwm_gpio_to_slice_num(pinos::kDisplayBacklight);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 1.0F);
    pwm_config_set_wrap(&cfg, kWrap);
    pwm_init(fatia, &cfg, true);
    define_duty(0);
}

void RetroiluminacaoPwm::define_duty(std::uint16_t duty) {
    duty_ = duty;
    pwm_set_gpio_level(pinos::kDisplayBacklight, nivel_de(duty));
}

}  // namespace coruja
