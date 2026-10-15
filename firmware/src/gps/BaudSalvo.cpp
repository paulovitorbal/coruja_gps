#include "gps/BaudSalvo.h"

#include <cstdio>

#include "gps/ConfiguradorGps.h"

namespace coruja {

std::size_t formata_baud(std::uint32_t baud, char* destino,
                         std::size_t capacidade) {
    if (destino == nullptr) { return 0; }
    if (baud != kBaudFabrica && baud != kBaudDesejado) {
        // Recusa na SAÍDA também: gravar um valor que a leitura vai rejeitar
        // deixaria o cartão com lixo que parece configuração.
        return 0;
    }
    const int n = std::snprintf(destino, capacidade, "baud=%lu\n",
                                static_cast<unsigned long>(baud));
    if (n <= 0 || static_cast<std::size_t>(n) >= capacidade) { return 0; }
    return static_cast<std::size_t>(n);
}

bool analisa_baud(const char* texto, std::size_t tamanho,
                  std::uint32_t* destino) {
    if (texto == nullptr || destino == nullptr) { return false; }

    const char kChave[] = "baud=";
    const std::size_t kTamChave = sizeof kChave - 1;
    if (tamanho <= kTamChave) { return false; }
    for (std::size_t i = 0; i < kTamChave; ++i) {
        if (texto[i] != kChave[i]) { return false; }
    }

    std::uint32_t valor = 0;
    std::size_t digitos = 0;
    for (std::size_t i = kTamChave; i < tamanho; ++i) {
        const char c = texto[i];
        if (c >= '0' && c <= '9') {
            if (digitos >= 7) { return false; }   // 115200 tem 6
            valor = valor * 10U + static_cast<std::uint32_t>(c - '0');
            ++digitos;
            continue;
        }
        // Fim do número: só espaço em branco pode vir depois.
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t' || c == '\0') {
            break;
        }
        return false;
    }
    if (digitos == 0) { return false; }
    if (valor != kBaudFabrica && valor != kBaudDesejado) { return false; }
    *destino = valor;
    return true;
}

}  // namespace coruja
