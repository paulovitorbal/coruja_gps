#include "nucleo/Texto.h"

#include <cstring>

namespace coruja {
namespace {

bool branco(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

}  // namespace

void apara_branco(char* texto, std::size_t* tamanho) {
    if (texto == nullptr || tamanho == nullptr) {
        return;
    }
    std::size_t inicio = 0;
    std::size_t fim = *tamanho;

    while (fim > inicio && branco(texto[fim - 1])) {
        --fim;
    }
    while (inicio < fim && branco(texto[inicio])) {
        ++inicio;
    }
    if (inicio > 0) {
        std::memmove(texto, texto + inicio, fim - inicio);
    }
    *tamanho = fim - inicio;
    texto[*tamanho] = '\0';
}

}  // namespace coruja
