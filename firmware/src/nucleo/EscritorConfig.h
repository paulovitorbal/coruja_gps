#pragma once
#include <cstddef>

#include "nucleo/Configuracao.h"

namespace coruja {

/// Reescreve no texto do coruja.cfg **so os valores dos ajustes**.
///
/// Cirurgica de proposito: o arquivo tem dois autores. A pessoa escreve
/// as redes, as URLs e os comentarios -- que sao a maior parte dele e a
/// unica documentacao do formato que fica junto do cartao -- e o aparelho
/// escreve os cinco ajustes do menu. Regerar o arquivo inteiro a partir da
/// struct apagaria tudo que o leitor nao guarda, comentarios inclusive, na
/// primeira vez que alguem mexesse no brilho.
///
/// Preserva byte a byte tudo que nao for o valor de uma chave conhecida,
/// incluindo indentacao, espacos em volta do '=' e o fim de linha (o '\r'
/// de um arquivo salvo no Windows sobrevive). Chaves ausentes sao
/// acrescentadas no fim.
///
/// Reescreve **todas** as ocorrencias de uma chave repetida, e nao so a
/// ultima que o leitor honra: deixar uma ocorrencia velha para tras faria
/// o arquivo mostrar dois valores para o mesmo ajuste.
///
/// Devolve quantos bytes foram escritos em `destino`, ou 0 se nao couberem
/// -- e nesse caso `destino` nao serve para nada. A saida nunca e vazia
/// quando ha espaco, entao 0 nao e ambiguo.
std::size_t reescreve_ajustes(const char* origem, std::size_t tamanho,
                              const Configuracao& cfg, char* destino,
                              std::size_t capacidade);

}  // namespace coruja
