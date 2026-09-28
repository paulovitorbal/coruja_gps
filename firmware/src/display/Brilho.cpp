#include "display/Brilho.h"

namespace coruja {
namespace {

/// `(pct/100)^2,2 × 65535`, para pct de 5 em 5. Gerada em Python e conferida
/// no ponto que importa: 50% de brilho **percebido** são 21,8% do duty, e é
/// essa diferença que a curva existe para produzir. Tabela em vez de `powf`
/// porque é 40 bytes de flash contra uma chamada de biblioteca por ajuste.
constexpr std::uint16_t kCurva[kPassosBrilho] = {
        90,    413,   1009,   1900,   3104,   //  5% a 25%
      4636,   6508,   8730,  11312,  14263,   // 30% a 50%
     17590,  21301,  25403,  29901,  34802,   // 55% a 75%
     40112,  45835,  51976,  58542,  65535,   // 80% a 100%
};

}  // namespace

void Brilho::aumenta() {
    if (passo_ < kPassosBrilho) { ++passo_; }
}

void Brilho::diminui() {
    // Para no 1, que é o piso de 5%. Zero apagaria a tela e o usuário
    // perderia a referência para recuperá-la.
    if (passo_ > 1) { --passo_; }
}

std::uint8_t Brilho::percentual() const {
    return static_cast<std::uint8_t>(passo_ * 5U);
}

std::uint16_t Brilho::duty() const { return kCurva[passo_ - 1]; }

}  // namespace coruja
