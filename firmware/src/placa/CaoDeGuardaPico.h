#pragma once
#include <cstdint>

#include "nucleo/CaoDeGuarda.h"

namespace coruja {

/// O cão de guarda de verdade, o do RP2350. Quatro linhas de SDK, e é onde o
/// hardware entra — todo o resto do firmware só conhece a interface.
///
/// ⚠️ **A leitura de `reiniciou_por_travamento()` tem de ser feita antes de
/// `arma()`**, porque `watchdog_enable` reescreve o registrador de rascunho
/// em que a resposta mora. No `main.cpp` ela é a primeira coisa que acontece.
class CaoDeGuardaPico : public CaoDeGuarda {
public:
    void arma(std::uint32_t prazo_ms) override;
    void alimenta() override;
    void suspende() override;
    void retoma() override;
    bool reiniciou_por_travamento() const override;

private:
    std::uint32_t prazo_ms_ = 0;
};

}  // namespace coruja
