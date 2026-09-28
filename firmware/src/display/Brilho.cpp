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

std::size_t& Brilho::passo_vigente() {
    return periodo_ == PeriodoDoDia::Noite ? passo_noite_ : passo_dia_;
}

std::size_t Brilho::passo_vigente() const {
    return periodo_ == PeriodoDoDia::Noite ? passo_noite_ : passo_dia_;
}

namespace {

/// Porcentagem para posicao de passo: 5% -> 1, 100% -> 20.
std::size_t passo_de_pct(std::uint8_t pct) {
    if (pct <= kBrilhoMinimoPct) { return 1; }
    if (pct >= kBrilhoMaximoPct) { return kPassosBrilho; }
    return static_cast<std::size_t>((pct + 2) / 5);
}

}  // namespace

void Brilho::define_presets(std::uint8_t dia_pct, std::uint8_t noite_pct) {
    passo_dia_ = passo_de_pct(dia_pct);
    passo_noite_ = passo_de_pct(noite_pct);
}

std::uint8_t Brilho::pct_dia() const {
    return static_cast<std::uint8_t>(passo_dia_ * 5);
}

std::uint8_t Brilho::pct_noite() const {
    return static_cast<std::uint8_t>(passo_noite_ * 5);
}

void Brilho::define_periodo(PeriodoDoDia periodo) {
    if (periodo == PeriodoDoDia::Desconhecido) { return; }
    periodo_ = periodo;
}

void Brilho::aumenta() {
    auto& p = passo_vigente();
    if (p < kPassosBrilho) { ++p; }
}

void Brilho::diminui() {
    // Para no 1, que é o piso de 5%. Zero apagaria a tela e o usuário
    // perderia a referência para recuperá-la.
    auto& p = passo_vigente();
    if (p > 1) { --p; }
}

std::size_t Brilho::passo() const { return passo_vigente(); }

std::uint8_t Brilho::percentual() const {
    return static_cast<std::uint8_t>(passo_vigente() * 5U);
}

std::uint16_t Brilho::duty() const { return kCurva[passo_vigente() - 1]; }

}  // namespace coruja
