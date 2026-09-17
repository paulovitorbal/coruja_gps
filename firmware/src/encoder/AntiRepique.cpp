#include "encoder/AntiRepique.h"

namespace coruja {

bool AntiRepique::amostra(bool ativo, std::uint32_t agora_ms) {
    if (ativo == estavel_) {
        // Voltou ao nível já aceito: o candidato anterior era repique.
        aguardando_ = false;
        return false;
    }

    if (!aguardando_ || ativo != candidato_) {
        candidato_  = ativo;
        desde_ms_   = agora_ms;
        aguardando_ = true;
    }

    // Subtração sem sinal: funciona através do wraparound de 32 bits, que
    // acontece a cada ~49 dias de `time_us_32()` e não pode gerar um evento
    // espúrio no carro. Com `janela_ms_` igual a zero a comparação é falsa de
    // saída, e o nível é aceito já na primeira amostra.
    if (agora_ms - desde_ms_ < janela_ms_) {
        return false;
    }

    estavel_    = candidato_;
    aguardando_ = false;
    return estavel_;  // só a borda para ativo é evento
}

}  // namespace coruja
