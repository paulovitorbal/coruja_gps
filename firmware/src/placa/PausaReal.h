#pragma once
#include "nucleo/Pausa.h"

namespace coruja {

/// A espera de verdade, do SDK. Uma linha, e é o ponto em que o hardware
/// entra — todo o resto da lógica de atualização o desconhece.
class PausaReal : public Pausa {
public:
    void espera_ms(std::uint32_t ms) override;
    std::uint32_t agora_ms() override;
};

}  // namespace coruja
