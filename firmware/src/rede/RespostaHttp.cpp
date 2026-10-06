#include "rede/RespostaHttp.h"

#include <cstring>

namespace coruja {
namespace {

bool branco(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/// -1 se nao for digito hexadecimal.
int hex(char c) {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

}  // namespace

std::uint32_t status_da_resposta(const char* texto, std::size_t tamanho) {
    // "HTTP/1.1 201 ..." -- o codigo comeca depois do primeiro espaco.
    constexpr std::size_t kMinimo = 12;  // "HTTP/x.y NNN"
    if (texto == nullptr || tamanho < kMinimo) { return 0; }
    if (std::memcmp(texto, "HTTP/", 5) != 0) { return 0; }

    std::size_t i = 5;
    while (i < tamanho && texto[i] != ' ') { ++i; }
    while (i < tamanho && texto[i] == ' ') { ++i; }
    if (i + 3 > tamanho) { return 0; }

    std::uint32_t codigo = 0;
    for (std::size_t j = 0; j < 3; ++j) {
        const char c = texto[i + j];
        if (c < '0' || c > '9') { return 0; }
        codigo = codigo * 10 + static_cast<std::uint32_t>(c - '0');
    }
    return codigo;
}

const char* corpo_da_resposta(const char* texto, std::size_t tamanho,
                              std::size_t* tamanho_do_corpo) {
    if (texto == nullptr || tamanho_do_corpo == nullptr) { return nullptr; }
    for (std::size_t i = 0; i + 1 < tamanho; ++i) {
        if (texto[i] == '\n' && texto[i + 1] == '\n') {
            *tamanho_do_corpo = tamanho - (i + 2);
            return texto + i + 2;
        }
        if (i + 3 < tamanho && std::memcmp(texto + i, "\r\n\r\n", 4) == 0) {
            *tamanho_do_corpo = tamanho - (i + 4);
            return texto + i + 4;
        }
    }
    return nullptr;
}

bool le_crc_hex(const char* texto, std::size_t tamanho,
                std::uint32_t* destino) {
    if (texto == nullptr || destino == nullptr) { return false; }

    std::size_t i = 0;
    while (i < tamanho && branco(texto[i])) { ++i; }
    if (i + 8 > tamanho) { return false; }

    std::uint32_t valor = 0;
    for (std::size_t j = 0; j < 8; ++j) {
        const int d = hex(texto[i + j]);
        if (d < 0) { return false; }
        valor = (valor << 4) | static_cast<std::uint32_t>(d);
    }
    i += 8;

    // Depois dos oito digitos so pode vir branco. Um nono digito significa
    // que o servidor mandou outra coisa, e ler os oito primeiros dela daria
    // um numero que parece um CRC.
    while (i < tamanho) {
        if (!branco(texto[i])) { return false; }
        ++i;
    }
    *destino = valor;
    return true;
}

}  // namespace coruja
