#pragma once
#include <cstdint>

#include "led/LedRgb.h"

namespace coruja {

/// LED RGB de **cátodo comum** em três canais de PWM.
///
/// Cátodo comum significa que o cátodo vai ao GND e cada ânodo é puxado para
/// alto através do seu resistor: nível alto acende, e o duty mapeia direto na
/// intensidade, sem inverter. Um LED de ânodo comum exigiria o inverso, e
/// ligar um no lugar do outro deixa o LED aceso ao contrário — daí a
/// polaridade estar no nome do tipo e não num parâmetro.
///
/// Pinos conforme `bom_schematic.md` §4. Os resistores diferem por canal
/// (330 Ω no vermelho, 68 Ω no verde e no azul), então a mesma intensidade
/// numérica **não** produz o mesmo brilho percebido nos três — é o que o R-05
/// vai medir e o que `ConfigCalibracao.h` vai corrigir.
class LedRgbPwm final : public LedRgb {
public:
    static constexpr unsigned kGpioVermelho = 6;
    static constexpr unsigned kGpioVerde    = 7;
    static constexpr unsigned kGpioAzul     = 8;

    /// 1 kHz é folgado para um LED: acima de ~200 Hz não há cintilação
    /// perceptível, e não há razão para subir mais e gastar resolução.
    static constexpr std::uint16_t kFrequenciaHz = 1000;

    LedRgbPwm(unsigned gpio_r = kGpioVermelho, unsigned gpio_g = kGpioVerde,
              unsigned gpio_b = kGpioAzul);

    void define_cor(const Cor& cor) override;
    Cor  cor_atual() const override { return atual_; }

private:
    void configura_canal(unsigned gpio);
    void escreve_canal(unsigned gpio, std::uint8_t intensidade);

    unsigned gpio_r_;
    unsigned gpio_g_;
    unsigned gpio_b_;
    Cor      atual_{};
};

}  // namespace coruja
