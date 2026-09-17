#pragma once
#include <cstdint>

namespace coruja {

/// Campo DirType do arquivo iGO8. E o DirType que indica omnidirecional,
/// nao o Direction igual a zero: 99,6% dos pontos tem Direction preenchido
/// mesmo quando DirType e zero (formato_dados.md §0.1).
enum class Sentido : std::uint8_t {
    Omnidirecional = 0,
    Unidirecional  = 1,
    Bidirecional   = 2,
};

constexpr bool sentido_valido(std::uint8_t bruto) {
    return bruto <= 2;
}

}  // namespace coruja
