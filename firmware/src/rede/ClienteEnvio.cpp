#include "rede/ClienteEnvio.h"

#include <lwip/dns.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include <cstdio>
#include <cstring>

#include "log/Logger.h"

namespace coruja {
namespace {

constexpr const char* kOrigem = "envio";

/// Estado de um pedido, compartilhado com os callbacks em C do lwIP.
///
/// Sem proteção contra concorrência pelo mesmo motivo do `ClienteHttp`: o
/// `cyw43_arch` está em modo **poll** e os callbacks rodam no contexto do laço
/// principal, chamados de dentro de `cyw43_arch_poll()`.
struct Pedido {
    // --- entrada ---
    Enviador::FonteDeBytes fonte = nullptr;
    void*         contexto = nullptr;
    std::size_t   corpo_total = 0;

    // --- andamento ---
    std::size_t   corpo_enviado = 0;
    bool          cabecalho_enviado = false;
    bool          corpo_completo = false;
    bool          terminou = false;
    bool          falhou = false;
    bool          conectou = false;

    // --- resposta ---
    char          resposta[kMaxRespostaEnvio] = {};
    std::size_t   resposta_n = 0;
};

void encerra(Pedido* p, struct tcp_pcb* pcb, bool erro) {
    if (erro) { p->falhou = true; }
    p->terminou = true;
    if (pcb != nullptr) {
        tcp_arg(pcb, nullptr);
        tcp_sent(pcb, nullptr);
        tcp_recv(pcb, nullptr);
        tcp_err(pcb, nullptr);
        tcp_close(pcb);
    }
}

/// Empurra o que couber na janela. Volta quando a janela fecha ou o corpo
/// acaba; o `ao_enviar` chama de novo a cada confirmacao.
bool bombeia(Pedido* p, struct tcp_pcb* pcb) {
    std::uint8_t pedaco[512];
    while (p->corpo_enviado < p->corpo_total) {
        const std::size_t janela = tcp_sndbuf(pcb);
        if (janela == 0) { return true; }  // sem espaco agora; volta depois

        const std::size_t falta = p->corpo_total - p->corpo_enviado;
        std::size_t quer = sizeof pedaco;
        if (quer > janela) { quer = janela; }
        if (quer > falta)  { quer = falta; }

        const std::size_t n = p->fonte(p->contexto, pedaco, quer);
        if (n == 0) {
            // A fonte secou antes do Content-Length prometido. Mandar menos
            // deixaria o servidor esperando bytes que nunca vem ate o tempo
            // dele estourar; abortar devolve o erro agora.
            return false;
        }
        // TCP_WRITE_FLAG_COPY: o `pedaco` e local e some na volta deste laco.
        const err_t r = tcp_write(pcb, pedaco, static_cast<u16_t>(n),
                                  TCP_WRITE_FLAG_COPY);
        if (r != ERR_OK) { return false; }
        p->corpo_enviado += n;
    }
    p->corpo_completo = true;
    return tcp_output(pcb) == ERR_OK;
}

err_t ao_enviar(void* arg, struct tcp_pcb* pcb, u16_t) {
    auto* p = static_cast<Pedido*>(arg);
    if (p == nullptr || p->terminou) { return ERR_OK; }
    if (!p->corpo_completo && !bombeia(p, pcb)) {
        encerra(p, pcb, true);
    }
    return ERR_OK;
}

err_t ao_receber(void* arg, struct tcp_pcb* pcb, struct pbuf* buf, err_t erro) {
    auto* p = static_cast<Pedido*>(arg);
    if (p == nullptr) { return ERR_OK; }

    if (buf == nullptr || erro != ERR_OK) {
        // Fim do fluxo. Nao e falha por si: o servidor pode ter respondido e
        // fechado, e a resposta ja esta guardada.
        if (buf != nullptr) { pbuf_free(buf); }
        encerra(p, pcb, false);
        return ERR_OK;
    }

    // Guarda so o que cabe. Depois dos cabecalhos e dos oito digitos do CRC
    // nao ha nada que este cliente leia.
    for (struct pbuf* parte = buf; parte != nullptr; parte = parte->next) {
        const std::size_t livre = kMaxRespostaEnvio - 1 - p->resposta_n;
        if (livre == 0) { break; }
        const std::size_t n = parte->len < livre ? parte->len : livre;
        std::memcpy(p->resposta + p->resposta_n, parte->payload, n);
        p->resposta_n += n;
    }
    p->resposta[p->resposta_n] = '\0';

    tcp_recved(pcb, buf->tot_len);
    pbuf_free(buf);
    return ERR_OK;
}

void ao_falhar(void* arg, err_t) {
    auto* p = static_cast<Pedido*>(arg);
    if (p == nullptr) { return; }
    // O pcb ja foi liberado pelo lwIP quando este callback roda.
    p->falhou = true;
    p->terminou = true;
}

/// Resolve o host, bloqueando. `false` se nao resolveu a tempo.
bool resolve(const char* host, ip_addr_t* destino, std::uint32_t limite_ms,
             Logger& log) {
    struct Resolucao {
        ip_addr_t endereco = {};
        bool      pronto = false;
        bool      achou = false;
    } r;

    auto ao_resolver = [](const char*, const ip_addr_t* ip, void* arg) {
        auto* res = static_cast<Resolucao*>(arg);
        res->pronto = true;
        if (ip != nullptr) {
            res->endereco = *ip;
            res->achou = true;
        }
    };

    cyw43_arch_lwip_begin();
    const err_t imediato = dns_gethostbyname(host, &r.endereco, ao_resolver, &r);
    cyw43_arch_lwip_end();

    if (imediato == ERR_OK) {
        *destino = r.endereco;
        return true;
    }
    if (imediato != ERR_INPROGRESS) {
        log.error(kOrigem, "DNS nao iniciou");
        return false;
    }

    const absolute_time_t prazo = make_timeout_time_ms(limite_ms);
    while (!r.pronto) {
        if (absolute_time_diff_us(get_absolute_time(), prazo) <= 0) {
            log.error(kOrigem, "DNS nao respondeu a tempo");
            return false;
        }
        cyw43_arch_poll();
        sleep_ms(1);
    }
    if (!r.achou) {
        log.error(kOrigem, "host nao resolveu");
        return false;
    }
    *destino = r.endereco;
    return true;
}

/// Faz o pedido inteiro: conecta, manda cabecalho e corpo, le a resposta.
ResultadoHttp conversa(const Url& url, const char* metodo, const char* token,
                       Enviador::FonteDeBytes fonte, void* contexto,
                       std::size_t corpo, Logger& log,
                       std::uint32_t tempo_limite_ms, Pedido* pedido) {
    ResultadoHttp saida;

    if (url.tls) {
        log.error(kOrigem, "URL https: este cliente nao fala TLS");
        saida.erro = ErroHttp::TlsNaoSuportado;
        return saida;
    }
    if (url.host[0] == '\0') {
        saida.erro = ErroHttp::UrlInvalida;
        return saida;
    }

    char msg[224];
    // O token NAO entra aqui, e nao pode entrar nunca. Aqui ele vai no
    // cabecalho, mas a consulta e mascarada do mesmo jeito: se um dia uma
    // URL de envio carregar segredo, esta linha ja esta certa.
    std::snprintf(msg, sizeof msg, "%s http://%s:%u%.*s (%u B)", metodo,
                  url.host, static_cast<unsigned>(url.porta),
                  static_cast<int>(tamanho_sem_consulta(url.caminho)),
                  url.caminho, static_cast<unsigned>(corpo));
    log.info(kOrigem, msg);

    ip_addr_t endereco = {};
    if (!resolve(url.host, &endereco, tempo_limite_ms, log)) {
        saida.erro = ErroHttp::NaoIniciou;
        return saida;
    }

    pedido->fonte = fonte;
    pedido->contexto = contexto;
    pedido->corpo_total = corpo;
    pedido->corpo_completo = corpo == 0;

    cyw43_arch_lwip_begin();
    struct tcp_pcb* pcb = tcp_new();
    if (pcb == nullptr) {
        cyw43_arch_lwip_end();
        log.error(kOrigem, "sem memoria para o socket");
        saida.erro = ErroHttp::NaoIniciou;
        return saida;
    }
    tcp_arg(pcb, pedido);
    tcp_sent(pcb, ao_enviar);
    tcp_recv(pcb, ao_receber);
    tcp_err(pcb, ao_falhar);

    const err_t partida = tcp_connect(
        pcb, &endereco, url.porta,
        [](void* arg, struct tcp_pcb* p, err_t e) -> err_t {
            auto* ped = static_cast<Pedido*>(arg);
            if (ped == nullptr) { return ERR_OK; }
            if (e != ERR_OK) { encerra(ped, p, true); return ERR_OK; }
            ped->conectou = true;
            return ERR_OK;
        });
    cyw43_arch_lwip_end();

    if (partida != ERR_OK) {
        cyw43_arch_lwip_begin();
        tcp_abort(pcb);
        cyw43_arch_lwip_end();
        log.error(kOrigem, "nao consegui conectar");
        saida.erro = ErroHttp::NaoIniciou;
        return saida;
    }

    // Monta o cabecalho. `Connection: close` de proposito: assim o fim do
    // fluxo marca o fim da resposta, e nao e preciso interpretar
    // Content-Length nem Transfer-Encoding so para ler oito digitos.
    char cabecalho[512];
    const int n_cab = std::snprintf(
        cabecalho, sizeof cabecalho,
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "X-Coruja-Token: %s\r\n"
        "Content-Length: %u\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Connection: close\r\n"
        "\r\n",
        metodo, url.caminho, url.host, token == nullptr ? "" : token,
        static_cast<unsigned>(corpo));
    if (n_cab <= 0 || static_cast<std::size_t>(n_cab) >= sizeof cabecalho) {
        cyw43_arch_lwip_begin();
        tcp_abort(pcb);
        cyw43_arch_lwip_end();
        log.error(kOrigem, "cabecalho nao coube");
        saida.erro = ErroHttp::UrlInvalida;
        return saida;
    }

    const absolute_time_t prazo = make_timeout_time_ms(tempo_limite_ms);
    while (!pedido->terminou) {
        if (absolute_time_diff_us(get_absolute_time(), prazo) <= 0) {
            cyw43_arch_lwip_begin();
            tcp_arg(pcb, nullptr);
            tcp_abort(pcb);
            cyw43_arch_lwip_end();
            log.error(kOrigem, "tempo esgotado");
            saida.erro = ErroHttp::TempoEsgotado;
            saida.recebidos = pedido->corpo_enviado;
            return saida;
        }
        if (pedido->conectou && !pedido->cabecalho_enviado) {
            cyw43_arch_lwip_begin();
            const err_t w = tcp_write(pcb, cabecalho,
                                      static_cast<u16_t>(n_cab),
                                      TCP_WRITE_FLAG_COPY);
            const bool ok = w == ERR_OK && (pedido->corpo_total == 0
                                            || bombeia(pedido, pcb))
                            && tcp_output(pcb) == ERR_OK;
            cyw43_arch_lwip_end();
            pedido->cabecalho_enviado = true;
            if (!ok) {
                cyw43_arch_lwip_begin();
                tcp_arg(pcb, nullptr);
                tcp_abort(pcb);
                cyw43_arch_lwip_end();
                log.error(kOrigem, "falha ao escrever o pedido");
                saida.erro = ErroHttp::Interrompida;
                return saida;
            }
        }
        cyw43_arch_poll();
        sleep_ms(1);
    }

    saida.recebidos = pedido->corpo_enviado;
    if (pedido->falhou) {
        log.error(kOrigem, "conexao interrompida");
        saida.erro = ErroHttp::Interrompida;
        return saida;
    }

    saida.status = status_da_resposta(pedido->resposta, pedido->resposta_n);
    if (saida.status == 0) {
        log.error(kOrigem, "resposta sem linha de status");
        saida.erro = ErroHttp::Interrompida;
        return saida;
    }
    // 2xx: 201 no PUT, 200 na consulta.
    if (saida.status < 200 || saida.status > 299) {
        std::snprintf(msg, sizeof msg, "status HTTP %u",
                      static_cast<unsigned>(saida.status));
        log.error(kOrigem, msg);
        saida.erro = ErroHttp::StatusNaoOk;
        return saida;
    }
    return saida;
}

}  // namespace

ResultadoHttp ClienteEnvio::envia(const Url& url, const char* token,
                                  FonteDeBytes fonte, void* contexto,
                                  std::size_t tamanho, Logger& log,
                                  std::uint32_t tempo_limite_ms) {
    if (fonte == nullptr || tamanho == 0) {
        ResultadoHttp saida;
        saida.erro = ErroHttp::NaoIniciou;
        return saida;
    }
    Pedido pedido;
    return conversa(url, "PUT", token, fonte, contexto, tamanho, log,
                    tempo_limite_ms, &pedido);
}

ResultadoHttp ClienteEnvio::consulta_crc(const Url& url, const char* token,
                                         std::uint32_t* crc, Logger& log,
                                         std::uint32_t tempo_limite_ms) {
    Pedido pedido;
    auto saida = conversa(url, "GET", token, nullptr, nullptr, 0, log,
                          tempo_limite_ms, &pedido);
    if (!saida.ok()) { return saida; }

    std::size_t n_corpo = 0;
    const char* corpo = corpo_da_resposta(pedido.resposta, pedido.resposta_n,
                                          &n_corpo);
    if (corpo == nullptr || !le_crc_hex(corpo, n_corpo, crc)) {
        log.error(kOrigem, "resposta sem CRC legivel");
        saida.erro = ErroHttp::Interrompida;
        return saida;
    }
    saida.recebidos = n_corpo;
    return saida;
}

}  // namespace coruja
