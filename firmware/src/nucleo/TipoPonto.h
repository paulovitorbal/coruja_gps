#pragma once
#include <cstdint>

namespace coruja {

/// Codigos TYPE do arquivo iGO8, conforme o mapeamento oficial do
/// formato_dados.md §0.2. NAO confundir com os codigos de filtro da fonte
/// de dados, que vivem num espaco diferente.
enum class TipoPonto : std::uint8_t {
    RadarFixo        = 1,
    SemaforoComRadar = 2,
    SemaforoCamera   = 3,
    RadarMovel       = 5,
};

/// Ha limite de velocidade a comparar? Falso apenas na camera de semaforo,
/// que fiscaliza avanco de sinal e nao velocidade (RF03.3).
constexpr bool afere_velocidade(TipoPonto t) {
    return t != TipoPonto::SemaforoCamera;
}

/// Fiscaliza avanco de sinal. Verdadeiro nos dois tipos de semaforo; o
/// SemaforoComRadar fiscaliza as duas coisas (R-26).
constexpr bool e_semaforo(TipoPonto t) {
    return t == TipoPonto::SemaforoComRadar || t == TipoPonto::SemaforoCamera;
}

/// Nome curto para console e visor.
constexpr const char* descreve(TipoPonto t) {
    switch (t) {
        case TipoPonto::RadarFixo:        return "radar fixo";
        case TipoPonto::SemaforoComRadar: return "semaforo c/ radar";
        case TipoPonto::SemaforoCamera:   return "semaforo camera";
        case TipoPonto::RadarMovel:       return "radar movel";
    }
    return "?";
}

constexpr bool tipo_valido(std::uint8_t bruto) {
    return bruto == 1 || bruto == 2 || bruto == 3 || bruto == 5;
}

}  // namespace coruja
