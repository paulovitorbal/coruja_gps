#include "rede/ResolucaoDns.h"

#include <lwip/dns.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include "log/Logger.h"
#include "nucleo/EsperaVigiada.h"

namespace coruja {

bool resolve_nome(const PedidoDns& pedido, const char* host,
                  ip_addr_t* destino, std::uint32_t limite_ms, Logger& log) {
    if (pedido.caixa == nullptr || pedido.callback == nullptr ||
            pedido.endereco == nullptr || host == nullptr ||
            destino == nullptr) {
        return false;
    }

    // `cache` e local e isso esta certo: o lwIP so escreve nele de forma
    // SINCRONA, quando o nome ja esta em cache, e nunca o guarda para depois.
    // Quem ele guarda e o `callback_arg`, e esse nao e ponteiro.
    ip_addr_t cache = {};
    const std::uint32_t geracao = pedido.caixa->abre();
    cyw43_arch_lwip_begin();
    const err_t imediato = dns_gethostbyname(
        host, &cache, pedido.callback,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(geracao)));
    cyw43_arch_lwip_end();

    if (imediato == ERR_OK) {
        pedido.caixa->abandona();
        *destino = cache;
        return true;
    }
    if (imediato != ERR_INPROGRESS) {
        pedido.caixa->abandona();
        log.error(pedido.origem, "DNS nao iniciou");
        return false;
    }

    const absolute_time_t prazo = make_timeout_time_ms(limite_ms);
    // A refeicao do cao mora na `espera_vigiada`, DEPOIS do poll: se o
    // `cyw43_arch_poll` travar por dentro, ela nao acontece e o cao morde; se
    // for so o DNS que nao responde, o `prazo` acima encerra. Um DNS de 15 s
    // somado ao handshake e a resposta passava dos 16,5 s do cao.
    const FimDaEspera fim = espera_vigiada(
        pedido.cao,
        [&] { return pedido.caixa->pronto(); },
        [&] { return absolute_time_diff_us(get_absolute_time(), prazo) <= 0; },
        [] {
            // `sleep_ms(1)` e nao espera ocupada: o stdio USB depende de um
            // alarme para chamar `tud_task()`, e um laco fechado o emudece.
            cyw43_arch_poll();
            sleep_ms(1);
        });
    if (fim == FimDaEspera::Expirou) {
        // O lwIP VAI chamar de volta, mais tarde, com esta geracao -- nao ha
        // como cancelar. Abandonar e o que faz essa chamada nao encontrar
        // ninguem. O cao ja foi alimentado: o log abaixo forca descarga.
        pedido.caixa->abandona();
        log.error(pedido.origem, "DNS nao respondeu a tempo");
        return false;
    }

    const bool achou = pedido.caixa->achou();
    // Antes de copiar, e antes de qualquer retorno: dai em diante a resposta
    // atrasada nao tem para onde escrever.
    pedido.caixa->abandona();
    if (!achou) {
        log.error(pedido.origem, "host nao resolveu");
        return false;
    }
    *destino = *pedido.endereco;
    return true;
}

}  // namespace coruja
