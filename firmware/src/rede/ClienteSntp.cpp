#include "rede/ClienteSntp.h"

#include <lwip/dns.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include <cstdio>
#include <cstring>

#include "log/Logger.h"
#include "rede/CaixaDns.h"
#include "rede/Sntp.h"

namespace coruja {
namespace {

constexpr const char* kOrigem = "ntp";

struct Espera {
    std::uint8_t resposta[kTamanhoPacoteNtp] = {};
    std::size_t  recebidos = 0;
    bool         chegou = false;
};

void ao_chegar(void* arg, struct udp_pcb*, struct pbuf* p, const ip_addr_t*,
               u16_t) {
    auto* e = static_cast<Espera*>(arg);
    if (p == nullptr) { return; }
    if (e != nullptr && !e->chegou) {
        const std::size_t n = p->tot_len < kTamanhoPacoteNtp ? p->tot_len
                                                             : kTamanhoPacoteNtp;
        // `pbuf_copy_partial` junta os pedacos da cadeia: a resposta cabe num
        // pacote, mas o lwIP pode entrega-la fragmentada em buffers.
        e->recebidos = pbuf_copy_partial(p, e->resposta,
                                         static_cast<u16_t>(n), 0);
        e->chegou = true;
    }
    pbuf_free(p);
}

/// A caixa do DNS deste cliente. Ver `CaixaDns`: o lwIP nao cancela um
/// `dns_gethostbyname`, e aqui o aperto e o pior do projeto -- o prazo e de
/// 5 s e o lwIP desiste em `DNS_MAX_RETRIES (4) x DNS_TMR_INTERVAL (1000 ms)`.
/// Os dois caem um em cima do outro, e isto roda em TODA sessao de rede, logo
/// depois de associar ao Wi-Fi, que e justamente quando o DNS demora.
CaixaDns g_dns;
ip_addr_t g_endereco_dns = {};

void ao_resolver(const char*, const ip_addr_t* ip, void* arg) {
    // `arg` e a geracao, por valor -- nao um ponteiro para a pilha de quem
    // pediu. E perguntar vem antes de copiar.
    const auto geracao =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(arg));
    if (g_dns.entrega(geracao, ip != nullptr) && ip != nullptr) {
        g_endereco_dns = *ip;
    }
}

}  // namespace

bool ClienteSntp::consulta(const char* servidor, std::int64_t* segundos,
                           Logger& log) {
    if (servidor == nullptr || servidor[0] == '\0' || segundos == nullptr) {
        return false;
    }

    char msg[128];
    std::snprintf(msg, sizeof msg, "consultando %s", servidor);
    log.info(kOrigem, msg);

    // --- resolve o nome ---
    //
    // `cache` e local e isso esta certo: o lwIP so escreve nele de forma
    // SINCRONA, quando o nome ja esta em cache. O que ele guarda para depois e
    // o `callback_arg`, e esse deixou de ser ponteiro.
    ip_addr_t cache = {};
    ip_addr_t endereco = {};
    const std::uint32_t geracao = g_dns.abre();
    cyw43_arch_lwip_begin();
    const err_t imediato = dns_gethostbyname(
        servidor, &cache, ao_resolver,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(geracao)));
    cyw43_arch_lwip_end();

    if (imediato == ERR_OK) {
        g_dns.abandona();
        endereco = cache;
    } else if (imediato != ERR_INPROGRESS) {
        g_dns.abandona();
        log.warning(kOrigem, "DNS nao iniciou");
        return false;
    } else {
        const absolute_time_t prazo = make_timeout_time_ms(kTempoLimiteNtpMs);
        while (!g_dns.pronto()) {
            if (absolute_time_diff_us(get_absolute_time(), prazo) <= 0) {
                // O lwIP VAI responder mais tarde, com esta geracao. Abandonar
                // e o que faz essa resposta nao encontrar ninguem.
                g_dns.abandona();
                log.warning(kOrigem, "DNS nao respondeu a tempo");
                return false;
            }
            cyw43_arch_poll();
            sleep_ms(1);
        }
        const bool achou = g_dns.achou();
        g_dns.abandona();
        if (!achou) {
            log.warning(kOrigem, "servidor de hora nao resolveu");
            return false;
        }
        endereco = g_endereco_dns;
    }

    // --- manda o pedido ---
    Espera espera;
    cyw43_arch_lwip_begin();
    struct udp_pcb* pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb != nullptr) {
        udp_recv(pcb, ao_chegar, &espera);
    }
    cyw43_arch_lwip_end();
    if (pcb == nullptr) {
        log.warning(kOrigem, "sem memoria para o socket");
        return false;
    }

    bool mandou = false;
    cyw43_arch_lwip_begin();
    struct pbuf* saida = pbuf_alloc(PBUF_TRANSPORT,
                                    static_cast<u16_t>(kTamanhoPacoteNtp),
                                    PBUF_RAM);
    if (saida != nullptr) {
        monta_pedido_ntp(static_cast<std::uint8_t*>(saida->payload));
        mandou = udp_sendto(pcb, saida, &endereco, kPortaNtp) == ERR_OK;
        pbuf_free(saida);
    }
    cyw43_arch_lwip_end();

    if (!mandou) {
        cyw43_arch_lwip_begin();
        udp_remove(pcb);
        cyw43_arch_lwip_end();
        log.warning(kOrigem, "nao consegui mandar o pedido");
        return false;
    }

    // --- espera ---
    const absolute_time_t prazo_resposta =
        make_timeout_time_ms(kTempoLimiteNtpMs);
    while (!espera.chegou) {
        if (absolute_time_diff_us(get_absolute_time(), prazo_resposta) <= 0) {
            cyw43_arch_lwip_begin();
            udp_remove(pcb);
            cyw43_arch_lwip_end();
            log.warning(kOrigem, "sem resposta a tempo");
            return false;
        }
        cyw43_arch_poll();
        sleep_ms(1);
    }

    cyw43_arch_lwip_begin();
    udp_remove(pcb);
    cyw43_arch_lwip_end();

    // --- le, com toda a validacao que o Sntp.cpp faz ---
    const auto erro = le_resposta_ntp(espera.resposta, espera.recebidos,
                                      segundos);
    if (erro != ErroNtp::Nenhum) {
        std::snprintf(msg, sizeof msg, "resposta recusada: %s", descreve(erro));
        log.warning(kOrigem, msg);
        return false;
    }
    return true;
}

}  // namespace coruja
