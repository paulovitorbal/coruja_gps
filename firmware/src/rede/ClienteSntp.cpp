#include "rede/ClienteSntp.h"

#include <lwip/dns.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include <cstdio>
#include <cstring>

#include "log/Logger.h"
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

struct Resolucao {
    ip_addr_t endereco = {};
    bool      pronto = false;
    bool      achou = false;
};

void ao_resolver(const char*, const ip_addr_t* ip, void* arg) {
    auto* r = static_cast<Resolucao*>(arg);
    r->pronto = true;
    if (ip != nullptr) { r->endereco = *ip; r->achou = true; }
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
    Resolucao r;
    cyw43_arch_lwip_begin();
    const err_t imediato = dns_gethostbyname(servidor, &r.endereco,
                                             ao_resolver, &r);
    cyw43_arch_lwip_end();

    if (imediato == ERR_OK) {
        r.achou = true;
        r.pronto = true;
    } else if (imediato != ERR_INPROGRESS) {
        log.warning(kOrigem, "DNS nao iniciou");
        return false;
    }

    const absolute_time_t prazo = make_timeout_time_ms(kTempoLimiteNtpMs);
    while (!r.pronto) {
        if (absolute_time_diff_us(get_absolute_time(), prazo) <= 0) {
            log.warning(kOrigem, "DNS nao respondeu a tempo");
            return false;
        }
        cyw43_arch_poll();
        sleep_ms(1);
    }
    if (!r.achou) {
        log.warning(kOrigem, "servidor de hora nao resolveu");
        return false;
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
        mandou = udp_sendto(pcb, saida, &r.endereco, kPortaNtp) == ERR_OK;
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
