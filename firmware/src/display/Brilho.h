#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Passos de ~5% do RF04: 20 posições, de 5% a 100%.
constexpr std::size_t kPassosBrilho = 20;
/// **Piso de 5%, e nunca 0%.** Com o visor totalmente apagado o usuário
/// perde a referência visual para recuperá-lo.
constexpr std::uint8_t kBrilhoMinimoPct = 5;
constexpr std::uint8_t kBrilhoMaximoPct = 100;

/// Frequência mínima do PWM do backlight.
///
/// Abaixo de ~1 kHz o painel cintila de forma perceptível na visão
/// periférica e pode produzir efeito estroboscópico com feições da estrada.
/// Entre 1 e 20 kHz alguns módulos assobiam. Daí os 20 kHz.
constexpr std::uint32_t kFrequenciaPwmHz = 20000;

/// Ajuste de brilho do RF04, com a curva perceptual.
///
/// **A curva não é enfeite.** A percepção humana de brilho é aproximadamente
/// logarítmica: com passos lineares de *duty cycle*, toda a mudança
/// perceptível acontece no fundo da escala e os dez cliques de cima não
/// fazem nada. A tabela abaixo é `(pct/100)^2,2`, gerada fora do código.
class Brilho {
public:
    void aumenta();
    void diminui();

    /// O que o usuário vê na barra: 5 a 100.
    std::uint8_t percentual() const;

    /// O que vai ao PWM, de 0 a 65535. **Nunca zero**, mesmo no piso.
    std::uint16_t duty() const;

    /// Posição do passo, 1 a 20. Para a barra de ajuste da faixa superior.
    std::size_t passo() const { return passo_; }

private:
    std::size_t passo_ = kPassosBrilho;   ///< começa em 100%
};

}  // namespace coruja
