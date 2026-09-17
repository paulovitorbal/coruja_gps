#include "nucleo/LimiarInfracao.h"

#include "nucleo/Ponto.h"

namespace coruja {

namespace {
constexpr std::uint8_t kLimiteFaixaFixa = 100;  ///< ate aqui o desconto e absoluto
constexpr float        kDescontoKmh     = 6.0F;
constexpr float        kDescontoFrac    = 0.05F;
}  // namespace

float velocidade_infracao(std::uint8_t limite_via) {
    if (limite_via == kSemLimite) {
        return 0.0F;
    }
    const auto limite = static_cast<float>(limite_via);
    return limite_via <= kLimiteFaixaFixa ? limite + kDescontoKmh
                                          : limite * (1.0F + kDescontoFrac);
}

float excesso_percentual(float velocidade, std::uint8_t limite_via) {
    const float limiar = velocidade_infracao(limite_via);
    if (limiar <= 0.0F || velocidade <= limiar) {
        return 0.0F;
    }
    return (velocidade - limiar) / limiar * 100.0F;
}

}  // namespace coruja
