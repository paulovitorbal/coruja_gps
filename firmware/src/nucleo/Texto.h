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

/// O texto só tem imprimíveis de ASCII, de `0x21` a `0x7E`?
///
/// É a guarda contra **divisão de requisição**. Um `\r\n` no meio do
/// `url_envio` ou do `token_aparelho` do `coruja.cfg` vira cabeçalho novo na
/// requisição que o aparelho manda — e o cartão é removível e relido a cada
/// ação (RNF03), então ele é fronteira de sistema como qualquer outra entrada.
///
/// Recusa o espaço de propósito: numa URL ele tem de vir percent-encoded, e
/// cru ele parte a linha `GET /caminho HTTP/1.1` em duas. Recusa acima de
/// `0x7E` pela mesma razão — UTF-8 cru em caminho de URL é erro de quem
/// escreveu, não coisa a adivinhar aqui.
///
/// Texto vazio passa: "sem token" é um estado legítimo, e quem decide o que
/// fazer com ele não é esta função. `nullptr` recusa.
bool seguro_para_cabecalho(const char* texto);

}  // namespace coruja
