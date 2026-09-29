#pragma once
#include "display/Retroiluminacao.h"

namespace coruja {

/// Porte da retroiluminacao no RP2350: PWM de hardware no pino de BL.
///
/// **>= 20 kHz, e nao e escolha estetica** (RF04): abaixo de ~1 kHz o painel
/// cintila na visao periferica e pode estroboscopar feicoes da estrada; entre
/// 1 e 20 kHz alguns modulos assobiam. O divisor e o wrap sao escolhidos para
/// manter os 16 bits de resolucao que o `Brilho` produz, e o `static_assert`
/// no .cpp quebra o build se a conta deixar de fechar.
class RetroiluminacaoPwm : public Retroiluminacao {
public:
    /// Configura o PWM e **comeca apagado**. O estado inicial importa: a
    /// tela so deve acender quando houver algo desenhado nela, senao o
    /// primeiro quadro e um retangulo branco no escuro.
    RetroiluminacaoPwm();

    void define_duty(std::uint16_t duty) override;
    std::uint16_t duty() const override { return duty_; }

private:
    std::uint16_t duty_ = 0;
};

}  // namespace coruja
