#pragma once

namespace coruja {

/// Interface do buzzer (regra 2). Quem decide **quando** apitar nunca toca em
/// GPIO, e é por isso que a cadência inteira do RF03.7 é testada no host.
///
/// O SFM-27 é **ativo**: tem oscilador próprio, e só precisa de alimentação
/// ligada ou desligada. Não há tom a sintetizar, e portanto não há PWM de
/// áudio aqui — o que o firmware controla é a **cadência**, não a frequência.
class Buzzer {
public:
    virtual ~Buzzer() = default;

    virtual void define(bool ligado) = 0;
    virtual bool ligado() const = 0;

    void silencia() { define(false); }
};

}  // namespace coruja
