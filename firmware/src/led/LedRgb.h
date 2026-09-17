#pragma once

#include "led/Cor.h"

namespace coruja {

/// Interface do LED RGB (regra 2). Quem decide a cor nunca toca em PWM, e
/// por isso a decisao e testavel no host contra `LedRgbMock`.
class LedRgb {
public:
    virtual ~LedRgb() = default;

    virtual void define_cor(const Cor& cor) = 0;
    virtual Cor cor_atual() const = 0;

    void apaga() { define_cor(cores::kApagado); }
};

}  // namespace coruja
