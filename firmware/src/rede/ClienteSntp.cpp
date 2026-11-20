#include "rede/ClienteSntp.h"

#include <lwip/dns.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>
#include <pico/cyw43_arch.h>
#include <pico/rand.h>
#include <pico/stdlib.h>

#include <cstdio>
#include <cstring>

#include "log/Logger.h"
#include "rede/CaixaDns.h"
#include "rede/ResolucaoDns.h"
#include "rede/Sntp.h"

namespace coruja {
namespace {

constexpr const char* kOrigem = "ntp";

struct Espera {
    std::uint8_t resposta[kTamanhoPacoteNtp] = {};
    std::size_t  recebidos = 0;
    bool         chegou = false;
    /// De quem se espera resposta. Um socket UDP aceita pacote de QUALQUER
    /// origem; sem conferir, o primeiro que chegasse a porta durante a
    /// espera seria a hora do aparelho. O nonce do `Sntp.cpp` e a guarda
    /// principal -- esta e a barata, que descarta antes de olhar o resto.
    ip_addr_t    servidor = {};
    /// Pacotes recusados pela origem. Vai para o log: um numero diferente de
    /// zero e alguem mandando pacote de hora para este aparelho.
    unsigned     de_outra_origem = 0;
};

void ao_chegar(void* arg, struct udp_pcb*, struct pbuf* p,
               const ip_addr_t* origem, u16_t porta) {
    auto* e = static_cast<Espera*>(arg);
    if (p == nullptr) { return; }
    if (e != nullptr && !e->chegou) {
        if (origem == nullptr || !ip_addr_eq(origem, &e->servidor) ||
                porta != kPortaNtp) {
            ++e->de_outra_origem;
            pbuf_free(p);
            return;
        }
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
/// 5 s e o lwIP so desiste de um servidor aos ~7 s (ver `kPrazoDnsMs`, no
/// `ClienteTls.cpp`). Com o DNS mudo a resposta chega sempre depois do prazo,
/// e isto roda em TODA sessao de rede, logo depois de associar ao Wi-Fi, que
/// e justamente quando o DNS demora. O mesmo prazo impede o NTP de chegar ao
/// segundo servidor de DNS quando o primeiro esta morto.
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

/// O pedido deste cliente: caixa, callback e endereco DESTA unidade de
/// traducao. Ver `PedidoDns`.
const PedidoDns g_pedido_dns{&g_dns, ao_resolver, &g_endereco_dns, kOrigem};

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
    // A espera mora no `ResolucaoDns`, compartilhada com o `ClienteTls`. O
    // que NAO se compartilha e a caixa: o lwIP guarda tambem o ponteiro da
    // funcao, e e isso que faz a resposta atrasada de um pedido do NTP so
    // poder chegar ao callback do NTP. Ver `PedidoDns`.
    ip_addr_t endereco = {};
    if (!resolve_nome(g_pedido_dns, servidor, &endereco, kTempoLimiteNtpMs,
                      log)) {
        return false;
    }

    // --- manda o pedido ---
    Espera espera;
    espera.servidor = endereco;
    // O sorteio que amarra a resposta ao pedido. Do TRNG do RP2350, a mesma
    // fonte da entropia do TLS -- ver `monta_pedido_ntp`.
    const std::uint64_t nonce = get_rand_64();
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
        monta_pedido_ntp(static_cast<std::uint8_t*>(saida->payload), nonce);
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
            // O numero de descartados vai junto: "sem resposta" com
            // pacotes recusados e outra historia -- alguem mandou hora para
            // este aparelho e o servidor nao respondeu.
            std::snprintf(msg, sizeof msg,
                          "sem resposta a tempo (%u pacotes de outra origem "
                          "descartados)", espera.de_outra_origem);
            log.warning(kOrigem, msg);
            return false;
        }
        cyw43_arch_poll();
        sleep_ms(1);
    }

    cyw43_arch_lwip_begin();
    udp_remove(pcb);
    cyw43_arch_lwip_end();

    if (espera.de_outra_origem > 0) {
        std::snprintf(msg, sizeof msg,
                      "%u pacotes de hora vieram de outra origem e foram "
                      "descartados", espera.de_outra_origem);
        log.warning(kOrigem, msg);
    }

    // --- le, com toda a validacao que o Sntp.cpp faz ---
    const auto erro = le_resposta_ntp(espera.resposta, espera.recebidos, nonce,
                                      segundos);
    if (erro != ErroNtp::Nenhum) {
        std::snprintf(msg, sizeof msg, "resposta recusada: %s", descreve(erro));
        log.warning(kOrigem, msg);
        return false;
    }
    return true;
}

}  // namespace coruja
