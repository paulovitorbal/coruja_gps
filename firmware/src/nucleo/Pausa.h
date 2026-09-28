#pragma once
#include <cstdint>

namespace coruja {

/// Espera bloqueante, injetada.
///
/// Parece exagero abstrair um `sleep_ms`, e não é: era a **última** amarra de
/// hardware na lógica de atualização. As três tentativas do RF05.2 esperam
/// 5 s entre si, e um teste que dormisse de verdade levaria 10 segundos para
/// exercitar o caminho de falha — a ponto de ninguém rodar. Com a espera
/// injetada, o teste **verifica** que esperou, e em quanto, sem esperar.
class Pausa {
public:
    virtual ~Pausa() = default;
    virtual void espera_ms(std::uint32_t ms) = 0;
};

}  // namespace coruja
