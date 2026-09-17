#include "encoder/DecodificadorQuadratura.h"

namespace coruja {

namespace {

/// Indexada por `(anterior << 2) | atual`, com o estado sendo `(clk << 1) | dt`.
///
/// Sinal positivo = horário. A sequência horária é 11 -> 01 -> 00 -> 10 -> 11,
/// que soma +4; a anti-horária é 11 -> 10 -> 00 -> 01 -> 11, que soma -4.
/// Os zeros em posições de mudança dupla são as transições impossíveis, que é
/// como o ruído entra — e sair com zero é o ponto da tabela.
constexpr int kTransicao[16] = {
    /* 00 -> 00 */  0,
    /* 00 -> 01 */ -1,
    /* 00 -> 10 */ +1,
    /* 00 -> 11 */  0,   // impossível
    /* 01 -> 00 */ +1,
    /* 01 -> 01 */  0,
    /* 01 -> 10 */  0,   // impossível
    /* 01 -> 11 */ -1,
    /* 10 -> 00 */ -1,
    /* 10 -> 01 */  0,   // impossível
    /* 10 -> 10 */  0,
    /* 10 -> 11 */ +1,
    /* 11 -> 00 */  0,   // impossível
    /* 11 -> 01 */ +1,
    /* 11 -> 10 */ -1,
    /* 11 -> 11 */  0,
};

}  // namespace

void DecodificadorQuadratura::reinicia() {
    acumulado_ = 0;
    primeira_  = true;
}

int DecodificadorQuadratura::amostra(bool clk, bool dt) {
    const auto atual =
        static_cast<std::uint8_t>((clk ? 0x02 : 0x00) | (dt ? 0x01 : 0x00));

    // A primeira amostra só estabelece a referência: sem estado anterior real
    // não existe transição, e inventar uma produziria um passo fantasma no
    // boot ou depois de `reinicia()`.
    if (primeira_) {
        estado_anterior_ = atual;
        primeira_        = false;
        return 0;
    }

    acumulado_ += kTransicao[(estado_anterior_ << 2) | atual];
    estado_anterior_ = atual;

    if (acumulado_ >= kQuartosPorDetente) {
        acumulado_ = 0;
        return invertido_ ? -1 : +1;
    }
    if (acumulado_ <= -kQuartosPorDetente) {
        acumulado_ = 0;
        return invertido_ ? +1 : -1;
    }
    return 0;
}

}  // namespace coruja
