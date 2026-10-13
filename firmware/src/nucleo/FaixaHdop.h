#pragma once

namespace coruja {

/// Quão boa é a geometria dos satélites, em faixas.
///
/// **Por que faixa e não porcentagem.** HDOP não tem teto de bondade: 0,7 é
/// melhor que 1,0, e não existe "100%". Converter para porcentagem exigiria
/// inventar um máximo, e o número resultante afirmaria uma precisão que
/// ninguém mediu. A faixa diz o que dá para dizer.
///
/// Os cortes são a convenção da indústria de GNSS, não números escolhidos
/// aqui. No NEO-M8N o erro horizontal fica perto de `HDOP × 2 a 3 m`, então
/// a faixa `Bom` (2 a 5) já significa dezenas de metros — o bastante para
/// pôr o carro na pista errada.
enum class FaixaHdop {
    Desconhecida,   ///< sem GGA, ou HDOP zero/negativo
    Ideal,          ///< < 1
    Excelente,      ///< 1 a 2
    Bom,            ///< 2 a 5
    Moderado,       ///< 5 a 10
    Ruim,           ///< ≥ 10
};

/// Quantas barras o indicador da tela acende no melhor caso.
constexpr int kBarrasHdop = 4;

/// ⚠️ Zero **não** é perfeição: é o que o módulo emite sem solução. Tratá-lo
/// como `Ideal` afirmaria a melhor precisão possível exatamente quando não
/// há nenhuma, que é o pior erro que este indicador poderia cometer.
FaixaHdop classifica_hdop(float hdop);

/// Barras acesas, de `kBarrasHdop` (ótimo) a zero. Cresce com a QUALIDADE,
/// não com o número — HDOP menor é melhor.
int barras_de_hdop(FaixaHdop faixa);

const char* descreve(FaixaHdop faixa);

}  // namespace coruja
