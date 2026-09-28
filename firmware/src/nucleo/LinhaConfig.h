#pragma once
#include <cstddef>

namespace coruja {

/// O que conta como linha de `chave=valor` no coruja.cfg.
///
/// **Existe para ter uma definicao so.** O leitor e o escritor precisam
/// concordar byte a byte sobre qual linha define qual chave: se o escritor
/// reescrevesse uma linha que o leitor ignora -- ou deixasse de reescrever
/// uma que o leitor honra -- o ajuste apareceria salvo no arquivo e o
/// aparelho continuaria com o valor antigo, sem nada indicando o desacordo.

bool e_espaco(char c);

/// Recorta espacos das duas pontas, ajustando ponteiro e tamanho no lugar.
void apara(const char*& ini, std::size_t& n);

bool igual(const char* ini, std::size_t n, const char* literal);

/// Uma linha ja aparada, vista como chave e valor.
struct ParChaveValor {
    bool valida = false;      ///< falso para linha vazia, comentario ou sem '='
    const char* chave = nullptr;
    std::size_t n_chave = 0;
    const char* valor = nullptr;
    std::size_t n_valor = 0;
    bool comentario = false;  ///< distingue '#' de linha malformada
    bool vazia = false;       ///< so espaco, ou nada
};

/// Divide no PRIMEIRO '=': senha de Wi-Fi pode conter '='.
/// Recebe a linha crua, sem o '\n'; apara por conta propria.
ParChaveValor divide(const char* linha, std::size_t n);

}  // namespace coruja
