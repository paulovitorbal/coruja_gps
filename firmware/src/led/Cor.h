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

// Cores de zona do RF03, §"Codificação visual". Os percentuais são os da
// tabela: amarelo é R 100% + G ~70%, rosa é R 100% + B ~40%.
//
// O rosa não é escolha estética. Os estados vizinhos na escala de gravidade
// são amarelo (R+G) e vermelho (R); uma cor intermediária de canal R+G vira
// vermelho quando o verde apaga e amarelo quando satura — colide com os dois
// vizinhos. R+B não colide com nenhum: some para vermelho de um lado e vira
// magenta do outro, que não é estado de nada.
constexpr Cor kAmarelo{255, 178, 0};
constexpr Cor kRosa{255, 0, 102};
}  // namespace cores

}  // namespace coruja
