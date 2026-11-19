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

/// Qual token acompanha uma requisição: o `da_chamada`, quando há um e ele
/// pode entrar num cabeçalho, ou o `guardado`, quando não há.
///
/// **O token de fora é dado externo**, como todo o resto do `coruja.cfg`: o
/// cartão é removível e relido a cada ação (RNF03). Um `\r\n` nele vira
/// cabeçalho a mais na requisição que o aparelho manda, montado por quem
/// escreveu no cartão. O `ClienteTls::define_token` já recusava o token que
/// o cliente guarda; o que vem por chamada passava direto.
///
/// Token torto devolve vazio — e **não** o guardado: cair no guardado
/// mascararia o erro do `coruja.cfg` com o segredo certo, e o aparelho
/// funcionaria com uma configuração quebrada. Sem token o servidor responde
/// 401, que é visível.
///
/// Mora aqui, e não dentro do cliente, porque o cliente só compila para o
/// RP2350 — e esta é decisão sobre texto vindo de fora, que é exatamente o
/// que precisa de teste.
///
/// Nunca devolve nulo: o resultado vai direto para o `%s` que monta a
/// requisição.
const char* token_para_cabecalho(const char* da_chamada, const char* guardado);

}  // namespace coruja
