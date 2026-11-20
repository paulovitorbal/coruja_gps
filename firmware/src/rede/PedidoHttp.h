#pragma once
#include <cstddef>

#include "nucleo/Url.h"

namespace coruja {

/// Quanto cabe numa requisição deste aparelho, com folga para a maior URL
/// (`kMaxUrl`), o maior token (`kMaxToken`) e os cabeçalhos fixos.
constexpr std::size_t kMaxPedidoHttp = 512;

/// Monta a linha de pedido e os cabeçalhos, até a linha em branco.
///
/// **Vive separado do cliente de propósito**, como o `RespostaHttp`: o
/// `ClienteTls` só compila para o RP2350, e estas são as decisões do pedido
/// — quando o `Content-Length` existe, quando o token acompanha, o que
/// acontece quando não cabe. Enterradas lá, seriam o trecho do caminho de
/// envio sem teste nenhum.
///
/// Devolve quantos bytes foram escritos, ou **zero quando não coube** — e aí
/// quem chama tem de desistir do pedido, porque um cabeçalho truncado vira
/// uma requisição que o servidor lê pela metade.
///
/// `corpo` zero omite o `Content-Length`. O que havia aqui era um ternário
/// com os DOIS RAMOS IGUAIS: a intenção de omitir estava escrita e nunca
/// acontecia, e todo GET saía com `Content-Length: 0`.
///
/// `token` nulo ou vazio omite o `X-Coruja-Token`. O segredo **nunca** entra
/// na URL: no log de acesso do servidor e de qualquer proxy, a consulta
/// aparece; o cabeçalho, não.
///
/// `Connection: close` porque o fim do fluxo marca o fim da resposta quando
/// o servidor não diz tamanho. `Accept-Encoding: identity` porque o
/// Cloudflare comprime por padrão, e um `radares.bin` em gzip chegaria com
/// CRC que não bate — este firmware não descomprime, e não deve passar a
/// descomprimir.
std::size_t monta_pedido_http(char* destino, std::size_t capacidade,
                              const char* metodo, const Url& url,
                              const char* token, std::size_t corpo);

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
/// Nunca devolve nulo: o resultado vai direto para o `monta_pedido_http`.
const char* token_para_cabecalho(const char* da_chamada, const char* guardado);

}  // namespace coruja
