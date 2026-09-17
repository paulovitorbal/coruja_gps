#pragma once
#include <cstdint>

#include "nucleo/Sentido.h"
#include "nucleo/TipoPonto.h"

namespace coruja {

/// Um ponto de fiscalizacao, como fica na RAM depois de carregado.
///
/// Coordenadas em graus decimais. O arquivo guarda inteiros escalados por
/// 1e5; aqui ficam em float porque o RP2350 tem FPU de precisao simples e a
/// busca compara diferencas, nao valores absolutos (formato_dados.md §4.1).
///
/// **O layout tem de fechar em 12 bytes**, o mesmo do registro em arquivo, e
/// e por isso que o rumo fica **quantizado** em vez de em graus. Guardar o
/// rumo como `uint16_t` de graus parece mais conveniente, mas o padding de
/// alinhamento empurra a struct para 16 bytes — 33% a mais, ou 71 KiB sobre a
/// base real, e 625 KiB no teto de 40.000 pontos, que **estoura os 520 KiB de
/// SRAM**. O orcamento inteiro do `formato_dados.md` §1 depende deste tamanho.
/// Ver `docs/adr/0006`.
struct Ponto {
    float        lat;
    float        lon;
    std::uint8_t limite;   ///< km/h; zero significa "sem limite a comparar"
    std::uint8_t rumo_q;   ///< graus / 2, de 0 a 179 — use `rumo_graus()`
    TipoPonto    tipo;
    Sentido      sentido;

    /// Rumo em graus, de 0 a 358 em passos de 2. A quantizacao vem do formato
    /// e e irrelevante para o RF02.3, cujo filtro tem tolerancia de dezenas de
    /// graus.
    constexpr std::uint16_t rumo_graus() const {
        return static_cast<std::uint16_t>(rumo_q) * 2U;
    }
};

/// Sentinela de "este ponto nao afere velocidade" no campo limite.
constexpr std::uint8_t kSemLimite = 0;

}  // namespace coruja
