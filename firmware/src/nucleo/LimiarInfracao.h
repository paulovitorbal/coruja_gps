#pragma once
#include <cstdint>

namespace coruja {

/// Limiar acima do qual a velocidade passa a gerar multa: o `V_infra` do
/// RF03.6.
///
/// A Resolucao do CONTRAN desconta 7 km/h ate 100 km/h e 7% acima disso. O
/// projeto usa valores deliberadamente mais conservadores — 6 km/h e 5% — para
/// que o alerta chegue antes do limiar real, nao junto com ele.
///
/// Retorna zero quando o ponto nao afere velocidade (`limite == kSemLimite`):
/// nesse caso nao existe limiar, e o chamador nunca deve entrar em Zona de
/// Perigo (RF03.3).
float velocidade_infracao(std::uint8_t limite_via);

/// Percentual de excesso sobre o limiar de infracao, usado para escolher a
/// faixa do buzzer (RF03.7). Ancorado em `V_infra`, nao no limite da via:
/// ancorar no limite deixaria as faixas de 10% e 20% vazias em ~80% dos
/// radares, porque 6 km/h ja sao 20% de um limite de 30 km/h (R-23).
float excesso_percentual(float velocidade, std::uint8_t limite_via);

}  // namespace coruja
