#pragma once
#include <cstdint>

#include "nucleo/Sentido.h"
#include "nucleo/TipoPonto.h"

namespace coruja {

/// Um ponto de fiscalizacao, como fica na RAM depois de carregado.
///
/// Coordenadas em graus decimais. O arquivo guarda inteiros escalados por
/// 1e5 (12 B por registro); aqui ficam em float porque o RP2350 tem FPU de
/// precisao simples e a busca compara diferencas, nao valores absolutos
/// (formato_dados.md §4.1).
struct Ponto {
    float        lat;
    float        lon;
    std::uint8_t limite;   ///< km/h; zero significa "sem limite a comparar"
    std::uint16_t rumo;    ///< graus, 0 a 358, quantizado em passos de 2
    TipoPonto    tipo;
    Sentido      sentido;
};

/// Sentinela de "este ponto nao afere velocidade" no campo limite.
constexpr std::uint8_t kSemLimite = 0;

}  // namespace coruja
