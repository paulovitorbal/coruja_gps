#pragma once
#include <cstdint>

namespace coruja {

/// Cor como intensidade por canal, 0 a 255. Nao e RGB565 de tela: e o que sai
/// nos tres PWM do LED. A escala real de cada canal depende dos resistores
/// medidos no R-05, e a conversao fica em quem escreve no hardware.
struct Cor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend constexpr bool operator==(const Cor& a, const Cor& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }
    friend constexpr bool operator!=(const Cor& a, const Cor& b) {
        return !(a == b);
    }
};

namespace cores {
constexpr Cor kApagado{0, 0, 0};
constexpr Cor kVermelho{255, 0, 0};
constexpr Cor kVerde{0, 255, 0};
constexpr Cor kAzul{0, 0, 255};
}  // namespace cores

}  // namespace coruja
