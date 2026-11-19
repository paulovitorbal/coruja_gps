#pragma once
#include <cstdint>

#include <lwip/dns.h>
#include <lwip/ip_addr.h>

#include "rede/CaixaDns.h"

namespace coruja {

class Logger;

/// Resolve um nome pelo lwIP, esperando sem travar o aparelho.
///
/// **Existe porque a mesma espera vivia duas vezes**, no `ClienteTls` e no
/// `ClienteSntp`: o `dns_gethostbyname`, os três desfechos imediatos
/// (resolvido na hora, em curso, não iniciou), o laço com `cyw43_arch_poll()`
/// e — o que mais importa — o `abandona()` em **todas** as saídas. Dois
/// lugares para acertar essa última linha são um lugar a mais do que se pode
/// garantir.
///
/// ⚠️ **A caixa e o callback continuam sendo de quem chama, um por unidade de
/// tradução.** Não é cerimônia: o lwIP guarda o ponteiro da FUNÇÃO junto com
/// o `arg`, então a resposta atrasada de um pedido do NTP só pode chegar ao
/// callback do NTP. Uma caixa compartilhada desfaria exatamente a garantia
/// que a `CaixaDns` existe para dar.
///
/// O `arg` que vai ao lwIP é o **número da geração**, por valor, e nunca um
/// ponteiro para pilha — ver `CaixaDns`.
struct PedidoDns {
    /// A caixa desta unidade de tradução, viva pelo programa inteiro.
    CaixaDns* caixa = nullptr;
    /// O callback desta unidade de tradução, que escreve em `endereco`.
    dns_found_callback callback = nullptr;
    /// Onde o callback deposita o endereço, também vivo pelo programa
    /// inteiro. Lido só depois de `achou()`.
    const ip_addr_t* endereco = nullptr;
    /// Etiqueta de origem no log: "http" ou "ntp".
    const char* origem = nullptr;
};

/// `true` com `destino` preenchido. Em qualquer saída, a caixa fica
/// abandonada — uma resposta que chegue depois não encontra ninguém.
bool resolve_nome(const PedidoDns& pedido, const char* host,
                  ip_addr_t* destino, std::uint32_t limite_ms, Logger& log);

}  // namespace coruja
