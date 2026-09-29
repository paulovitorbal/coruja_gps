#pragma once
#include <cstdint>

namespace coruja {

/// Interface da retroiluminacao (regra 2).
///
/// Quem decide **quanto** brilho nunca toca em PWM, e e por isso que a curva
/// perceptual inteira do RF04 e testada no host. O que passa por aqui e o
/// duty bruto de 16 bits que o `Brilho` ja calculou.
class Retroiluminacao {
public:
    virtual ~Retroiluminacao() = default;

    /// Duty de 0 a 65535. **Zero apaga a tela** -- o `Brilho` nunca o
    /// produz, com piso de 5%, mas a porta aceita porque desligar o painel
    /// e uma operacao legitima de quem chama, nao um valor de ajuste.
    virtual void define_duty(std::uint16_t duty) = 0;

    virtual std::uint16_t duty() const = 0;
};

}  // namespace coruja
