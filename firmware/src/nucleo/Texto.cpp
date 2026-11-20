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

bool seguro_para_cabecalho(const char* texto) {
    if (texto == nullptr) { return false; }
    // `unsigned char` de propósito -- e isto NÃO muda o comportamento hoje:
    // com `char` com sinal, 0xC3 vira -61, cai no `< 0x21` e é recusado do
    // mesmo jeito. Os dois recusam exatamente o mesmo conjunto, e a campanha
    // de mutação confirmou que o mutante com sinal é equivalente.
    //
    // Fica assim pelo dia em que alguém mexer na comparação: com sinal, a
    // faixa que o código diz aceitar não é a que ele aceita, e metade dos
    // bytes é recusada pelo limite errado -- certo por acidente.
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(texto);
         *p != '\0'; ++p) {
        if (*p < 0x21 || *p > 0x7E) { return false; }
    }
    return true;
}

}  // namespace coruja
