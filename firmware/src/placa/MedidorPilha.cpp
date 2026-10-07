#include "placa/MedidorPilha.h"

namespace coruja {

std::size_t palavras_intocadas(const std::uint32_t* base,
                               std::size_t quantas) {
    if (base == nullptr) { return 0; }
    std::size_t n = 0;
    while (n < quantas && base[n] == kTintaDaPilha) { ++n; }
    return n;
}

}  // namespace coruja
