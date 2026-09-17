#pragma once

#include "encoder/EventoEncoder.h"

namespace coruja {

/// Interface do encoder rotativo (regra 2).
class Encoder {
public:
    virtual ~Encoder() = default;

    /// Devolve o proximo evento pendente, ou `Nenhum`. Consome o evento:
    /// chamar de novo sem que nada aconteca devolve `Nenhum`.
    virtual EventoEncoder proximo_evento() = 0;
};

}  // namespace coruja
