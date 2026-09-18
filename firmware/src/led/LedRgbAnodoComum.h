#pragma once
#include <cstdint>

#include "led/LedRgb.h"

namespace coruja {

/// LED RGB de **ânodo comum** em três canais de PWM.
///
/// Ânodo comum significa que o ânodo — o terminal mais longo — vai ao trilho de
/// 3,3 V, e cada cátodo desce por seu resistor até um GPIO. O GPIO **drena**
/// corrente em vez de fornecer, e **nível baixo acende**: a lógica é
/// invertida em relação a um LED de cátodo comum.
///
/// A polaridade está no **nome do tipo**, não num parâmetro de construtor, de
/// propósito. Ela não é configuração: é uma propriedade física da peça soldada,
/// e trocar um pelo outro em tempo de execução não faz sentido nenhum. Com o
/// nome no tipo, montar o LED errado é erro de compilação no lugar onde se
/// escolhe a classe, e não um LED aceso ao contrário que ninguém entende.
///
/// > O projeto assumia cátodo comum até 2026-09-18, quando o autor verificou
/// > que a peça em casa é **10 mm difuso, ânodo comum**. Ver **R-33**.
///
/// Pinos conforme `bom_schematic.md` §4. Os resistores diferem por canal
/// (330 Ω no vermelho, 68 Ω no verde e no azul), então a mesma intensidade
/// numérica **não** produz o mesmo brilho percebido nos três — é o que o R-05
/// vai medir e o que `ConfigCalibracao.h` vai corrigir.
class LedRgbAnodoComum final : public LedRgb {
public:
    static constexpr unsigned kGpioVermelho = 6;
    static constexpr unsigned kGpioVerde    = 7;
    static constexpr unsigned kGpioAzul     = 8;

    /// 1 kHz é folgado para um LED: acima de ~200 Hz não há cintilação
    /// perceptível, e não há razão para subir mais e gastar resolução.
    static constexpr std::uint16_t kFrequenciaHz = 1000;

    LedRgbAnodoComum(unsigned gpio_r = kGpioVermelho,
                     unsigned gpio_g = kGpioVerde,
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
