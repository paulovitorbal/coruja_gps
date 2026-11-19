#include "nucleo/LinhaConfig.h"

#include <cstring>

#include "nucleo/Texto.h"

namespace coruja {

void apara(const char*& ini, std::size_t& n) {
    while (n > 0 && e_branco(*ini)) {
        ++ini;
        --n;
    }
    while (n > 0 && e_branco(ini[n - 1])) {
        --n;
    }
}

bool igual(const char* ini, std::size_t n, const char* literal) {
    return std::strlen(literal) == n && std::strncmp(ini, literal, n) == 0;
}

ParChaveValor divide(const char* linha, std::size_t n) {
    ParChaveValor p;
    apara(linha, n);
    if (n == 0) {
        p.vazia = true;
        return p;
    }
    if (linha[0] == '#') {
        p.comentario = true;
        return p;
    }

    std::size_t pos = 0;
    while (pos < n && linha[pos] != '=') {
        ++pos;
    }
    if (pos == n) {
        return p;  // sem '=': malformada
    }

    p.chave = linha;
    p.n_chave = pos;
    p.valor = linha + pos + 1;
    p.n_valor = n - pos - 1;
    apara(p.chave, p.n_chave);
    apara(p.valor, p.n_valor);
    p.valida = true;
    return p;
}

}  // namespace coruja
