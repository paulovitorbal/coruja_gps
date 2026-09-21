#pragma once
#include <cstddef>

namespace coruja {

/// Tira espaço, tabulação, `\r` e `\n` das duas pontas, no lugar.
///
/// Existe por um motivo específico: a resposta da consulta de versão termina
/// em `\n`, e a comparação com a versão guardada é feita **como texto**. Sem
/// aparar, `"v1\n"` nunca é igual a `"v1"` e o aparelho rebaixa a mesma base a
/// cada clique — uma falha silenciosa, que só aparece como consumo de rede.
///
/// `tamanho` entra com o comprimento atual e sai com o novo. O texto fica
/// sempre terminado em `\0`.
void apara_branco(char* texto, std::size_t* tamanho);

}  // namespace coruja
