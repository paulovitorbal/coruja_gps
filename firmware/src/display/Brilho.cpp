#include "display/Brilho.h"

namespace coruja {
namespace {

/// Duty de cada passo, já com as duas transformações aplicadas.
///
/// `duty = (fisico/100)^2,2 × 65535`, onde
/// `fisico = kPisoFisicoPct + (100 - kPisoFisicoPct) × passo / 20`.
///
/// São duas coisas em cima da outra, e vale separá-las:
///
/// 1. **A redistribuição** mapeia os 0–100% que o usuário vê nos 10–100%
///    que o painel consegue. É linear, e existe para o piso do hardware não
///    vazar para a interface.
/// 2. **A curva de 2,2** é perceptual: a percepção humana de brilho é
///    aproximadamente logarítmica, e com passos lineares de duty toda a
///    mudança acontece no fundo da escala e os dez cliques de cima não
///    fazem nada.
///
/// Gerada em Python e conferida no ponto que importa: 50% na escala do
/// usuário dá 17.590 de duty, 26,8% do máximo — bem longe dos 50% que o
/// linear daria. Tabela em vez de `powf` porque são 42 bytes de flash
/// contra uma chamada de biblioteca por ajuste.
constexpr std::uint16_t kCurva[kPassosBrilho] = {
      413,   936,  1697,  2709,  3983,   //  0% a  20%
     5529,  7354,  9466, 11872, 14579,   // 25% a  45%
    17590, 20913, 24551, 28510, 32793,   // 50% a  70%
    37406, 42351, 47633, 53255, 59222,   // 75% a  95%
    65535,                               // 100%
};

}  // namespace

std::size_t& Brilho::passo_vigente() {
    return periodo_ == PeriodoDoDia::Noite ? passo_noite_ : passo_dia_;
}

std::size_t Brilho::passo_vigente() const {
    return periodo_ == PeriodoDoDia::Noite ? passo_noite_ : passo_dia_;
}

namespace {

/// Porcentagem para posicao de passo: 0% -> 0, 100% -> 20.
///
/// O passo 0 agora EXISTE e e o mais escuro utilizavel, nao a tela
/// apagada: quem apaga e o duty zero, que a curva nunca produz.
std::size_t passo_de_pct(std::uint8_t pct) {
    if (pct >= kBrilhoMaximoPct) { return kPassosBrilho - 1; }
    // +2 arredonda para o passo mais proximo em vez de truncar.
    return static_cast<std::size_t>((pct + 2) / 5);
}

/// A inversa. Uma funcao so, para as tres chamadas nao divergirem.
std::uint8_t pct_de_passo(std::size_t passo) {
    return static_cast<std::uint8_t>(passo * 5);
}

}  // namespace

void Brilho::define_presets(std::uint8_t dia_pct, std::uint8_t noite_pct) {
    passo_dia_ = passo_de_pct(dia_pct);
    passo_noite_ = passo_de_pct(noite_pct);
}

std::uint8_t Brilho::pct_dia() const { return pct_de_passo(passo_dia_); }

std::uint8_t Brilho::pct_noite() const { return pct_de_passo(passo_noite_); }

void Brilho::define_periodo(PeriodoDoDia periodo) {
    if (periodo == PeriodoDoDia::Desconhecido) { return; }
    periodo_ = periodo;
}

void Brilho::aumenta() {
    auto& p = passo_vigente();
    if (p + 1 < kPassosBrilho) { ++p; }
}

void Brilho::diminui() {
    // Para no passo 0, que na escala do usuário é 0% e no painel é
    // `kPisoFisicoPct`. A tela nunca apaga: com o visor escuro o usuário
    // perderia a referência para recuperá-lo, e a bancada mostrou que
    // "apagado na prática" começa antes do duty zero.
    auto& p = passo_vigente();
    if (p > 0) { --p; }
}

std::size_t Brilho::passo() const { return passo_vigente(); }

std::uint8_t Brilho::percentual() const {
    return pct_de_passo(passo_vigente());
}

std::uint16_t Brilho::duty() const { return kCurva[passo_vigente()]; }

}  // namespace coruja
