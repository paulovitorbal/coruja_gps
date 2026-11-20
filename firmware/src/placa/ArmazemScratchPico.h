#pragma once
#include <cstdint>

#include <hardware/watchdog.h>

#include "nucleo/PoliticaDoCao.h"

namespace coruja {

/// O contador de mordidas, guardado em `watchdog_hw->scratch[0]`.
///
/// Os registradores de rascunho do watchdog sobrevivem a um reset POR
/// watchdog. O SDK usa os de 4 a 7 (`watchdog.c`: `scratch[4]` e a marca de
/// `watchdog_enable`, 5-7 sao o destino do `watchdog_reboot`), entao o 0 esta
/// livre. Num religamento a frio o conteudo e indefinido, e e por isso que a
/// `PoliticaDoCao` poe uma marca junto da contagem.
class ArmazemScratchPico : public ArmazemDePalavra {
public:
    std::uint32_t le() const override { return watchdog_hw->scratch[0]; }
    void grava(std::uint32_t valor) override {
        watchdog_hw->scratch[0] = valor;
    }
};

}  // namespace coruja
