#include "rede/ClienteTls.h"

#include <lwip/altcp.h>
#include <lwip/altcp_tcp.h>
#include <lwip/altcp_tls.h>
#include <lwip/dns.h>
#include <lwip/pbuf.h>
#include <mbedtls/ssl.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include <cstdio>
#include <cstring>

#include "log/Logger.h"
#include "rede/RaizesConfiaveis.h"
#include "rede/RespostaHttp.h"

namespace coruja {
namespace {

constexpr const char* kOrigem = "http";

/// Estado de um pedido, compartilhado com os callbacks em C do lwIP.
///
/// Sem protecao contra concorrencia: o `cyw43_arch` esta em modo *poll* e os
/// callbacks rodam no contexto do laco principal, de dentro de
/// `cyw43_arch_poll()`.
struct Pedido {
    // --- envio ---
    Enviador::FonteDeBytes fonte = nullptr;
    void*       ctx_fonte = nullptr;
    std::size_t corpo_total = 0;
    std::size_t corpo_enviado = 0;
    bool        cabecalho_enviado = false;
    bool        corpo_completo = false;

    // --- recepcao ---
    LeitorRespostaHttp  leitor;
    Baixador::AoReceber ao_receber = nullptr;
    void*               ctx_recebe = nullptr;

    // --- andamento ---
    bool conectou = false;
    bool terminou = false;
    bool falhou = false;
    bool malformada = false;
    bool fonte_secou = false;
};

void encerra(Pedido* p, struct altcp_pcb* pcb, bool erro) {
    if (erro) { p->falhou = true; }
    p->terminou = true;
    if (pcb != nullptr) {
        altcp_arg(pcb, nullptr);
        altcp_sent(pcb, nullptr);
        altcp_recv(pcb, nullptr);
        altcp_err(pcb, nullptr);
        altcp_close(pcb);
    }
}

/// Empurra o que couber na janela. Volta quando ela fecha ou o corpo acaba.
bool bombeia(Pedido* p, struct altcp_pcb* pcb) {
    std::uint8_t pedaco[512];
    while (p->corpo_enviado < p->corpo_total) {
        const std::size_t janela = altcp_sndbuf(pcb);
        if (janela == 0) { return true; }

        const std::size_t falta = p->corpo_total - p->corpo_enviado;
        std::size_t quer = sizeof pedaco;
        if (quer > janela) { quer = janela; }
        if (quer > falta)  { quer = falta; }

        const std::size_t n = p->fonte(p->ctx_fonte, pedaco, quer);
        if (n == 0) {
            // A fonte secou antes do Content-Length prometido. Mandar menos
            // deixaria o servidor esperando bytes que nunca vem.
            p->fonte_secou = true;
            return false;
        }
        // TCP_WRITE_FLAG_COPY: `pedaco` e local e some na volta deste laco.
        if (altcp_write(pcb, pedaco, static_cast<u16_t>(n),
                        TCP_WRITE_FLAG_COPY) != ERR_OK) {
            return false;
        }
        p->corpo_enviado += n;
    }
    p->corpo_completo = true;
    return altcp_output(pcb) == ERR_OK;
}

err_t ao_enviar(void* arg, struct altcp_pcb* pcb, u16_t) {
    auto* p = static_cast<Pedido*>(arg);
    if (p == nullptr || p->terminou) { return ERR_OK; }
    if (!p->corpo_completo && !bombeia(p, pcb)) { encerra(p, pcb, true); }
    return ERR_OK;
}

err_t ao_receber(void* arg, struct altcp_pcb* pcb, struct pbuf* buf,
                 err_t erro) {
    auto* p = static_cast<Pedido*>(arg);
    if (p == nullptr) { return ERR_OK; }

    if (buf == nullptr || erro != ERR_OK) {
        // Fim do fluxo. Nao e falha por si: numa resposta sem
        // Content-Length nem pedacos, o fechamento E o fim do corpo.
        if (buf != nullptr) { pbuf_free(buf); }
        encerra(p, pcb, false);
        return ERR_OK;
    }

    const u16_t total = buf->tot_len;
    for (struct pbuf* parte = buf; parte != nullptr; parte = parte->next) {
        if (!p->leitor.alimenta(static_cast<const std::uint8_t*>(parte->payload),
                                parte->len, p->ao_receber, p->ctx_recebe)) {
            p->malformada = true;
            pbuf_free(buf);
            encerra(p, pcb, true);
            return ERR_OK;
        }
    }

    altcp_recved(pcb, total);
    pbuf_free(buf);

    // Corpo completo pelo Content-Length ou pelo pedaco zero: nao ha o que
    // esperar. Fechar aqui evita segurar a conexao ate o tempo limite.
    if (p->leitor.completa()) { encerra(p, pcb, false); }
    return ERR_OK;
}

void ao_falhar(void* arg, err_t) {
    auto* p = static_cast<Pedido*>(arg);
    if (p == nullptr) { return; }
    // O pcb ja foi liberado pelo lwIP quando este callback roda.
    p->falhou = true;
    p->terminou = true;
}

err_t ao_conectar(void* arg, struct altcp_pcb* pcb, err_t e) {
    auto* p = static_cast<Pedido*>(arg);
    if (p == nullptr) { return ERR_OK; }
    if (e != ERR_OK) { encerra(p, pcb, true); return ERR_OK; }
    p->conectou = true;
    return ERR_OK;
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

bool resolve(const char* host, ip_addr_t* destino, std::uint32_t limite_ms,
             Logger& log) {
    Resolucao r;
    cyw43_arch_lwip_begin();
    const err_t imediato = dns_gethostbyname(host, &r.endereco, ao_resolver, &r);
    cyw43_arch_lwip_end();

    if (imediato == ERR_OK) { *destino = r.endereco; return true; }
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

}  // namespace

ClienteTls::~ClienteTls() { libera_configuracao(); }

void ClienteTls::define_token(const char* token) {
    std::snprintf(token_, sizeof token_, "%s", token == nullptr ? "" : token);
}

void ClienteTls::libera_configuracao() {
    if (configuracao_ != nullptr) {
        altcp_tls_free_config(static_cast<struct altcp_tls_config*>(
            configuracao_));
        configuracao_ = nullptr;
    }
}

/// O dialogo inteiro: resolve, conecta, manda, le.
///
/// Uma funcao so para GET e PUT porque a diferenca entre eles e o metodo, o
/// corpo e para onde vai a resposta -- tudo parametro. Duas copias disto foi
/// o que o `ClienteHttp` e o `ClienteEnvio` eram.
static ResultadoHttp conversa(void** configuracao, const char* token,
                              const Url& url, const char* metodo,
                              Enviador::FonteDeBytes fonte, void* ctx_fonte,
                              std::size_t corpo,
                              Baixador::AoReceber ao_receber_corpo,
                              void* ctx_recebe, Logger& log,
                              std::uint32_t tempo_limite_ms) {
    ResultadoHttp saida;
    char msg[224];

    if (url.host[0] == '\0') {
        saida.erro = ErroHttp::UrlInvalida;
        return saida;
    }

    // O caminho vai SEM a consulta: e la que um segredo poderia viajar, e o
    // log vai para o cartao. O token nunca entra aqui.
    std::snprintf(msg, sizeof msg, "%s %s://%s:%u%.*s", metodo,
                  url.tls ? "https" : "http", url.host,
                  static_cast<unsigned>(url.porta),
                  static_cast<int>(tamanho_sem_consulta(url.caminho)),
                  url.caminho);
    log.info(kOrigem, msg);

    ip_addr_t endereco = {};
    if (!resolve(url.host, &endereco, tempo_limite_ms, log)) {
        saida.erro = ErroHttp::NaoIniciou;
        return saida;
    }

    Pedido pedido;
    pedido.fonte = fonte;
    pedido.ctx_fonte = ctx_fonte;
    pedido.corpo_total = corpo;
    pedido.corpo_completo = corpo == 0;
    pedido.ao_receber = ao_receber_corpo;
    pedido.ctx_recebe = ctx_recebe;
    pedido.leitor.reinicia();

    cyw43_arch_lwip_begin();
    struct altcp_pcb* pcb = nullptr;
    if (url.tls) {
        if (*configuracao == nullptr) {
            // As tres raizes, interpretadas uma vez e reaproveitadas. O
            // tamanho INCLUI o terminador, como o mbedtls_x509_crt_parse
            // exige para PEM.
            *configuracao = altcp_tls_create_config_client(
                reinterpret_cast<const std::uint8_t*>(kRaizesConfiaveis),
                kTamanhoRaizes);
        }
        if (*configuracao != nullptr) {
            pcb = altcp_tls_new(
                static_cast<struct altcp_tls_config*>(*configuracao),
                IPADDR_TYPE_V4);
        }
    } else {
        pcb = altcp_tcp_new_ip_type(IPADDR_TYPE_V4);
    }
    cyw43_arch_lwip_end();

    if (pcb == nullptr) {
        log.error(kOrigem, url.tls ? "sem memoria para a sessao TLS"
                                   : "sem memoria para o socket");
        saida.erro = ErroHttp::NaoIniciou;
        return saida;
    }

    if (url.tls) {
        // SNI. **O Cloudflare nao escolhe certificado sem isto** -- um
        // endereco so atende milhoes de dominios. A falha sem SNI e um
        // handshake recusado com mensagem que nao explica nada.
        cyw43_arch_lwip_begin();
        auto* ssl = static_cast<mbedtls_ssl_context*>(altcp_tls_context(pcb));
        const int r = ssl != nullptr
                      ? mbedtls_ssl_set_hostname(ssl, url.host) : -1;
        cyw43_arch_lwip_end();
        if (r != 0) {
            cyw43_arch_lwip_begin();
            altcp_abort(pcb);
            cyw43_arch_lwip_end();
            log.error(kOrigem, "nao consegui definir o SNI");
            saida.erro = ErroHttp::NaoIniciou;
            return saida;
        }
    }

    cyw43_arch_lwip_begin();
    altcp_arg(pcb, &pedido);
    altcp_sent(pcb, ao_enviar);
    altcp_recv(pcb, ao_receber);
    altcp_err(pcb, ao_falhar);
    const err_t partida = altcp_connect(pcb, &endereco, url.porta,
                                        ao_conectar);
    cyw43_arch_lwip_end();

    if (partida != ERR_OK) {
        cyw43_arch_lwip_begin();
        altcp_abort(pcb);
        cyw43_arch_lwip_end();
        log.error(kOrigem, "nao consegui conectar");
        saida.erro = ErroHttp::NaoIniciou;
        return saida;
    }

    // `Connection: close` de proposito: o fim do fluxo marca o fim da
    // resposta quando o servidor nao diz tamanho.
    //
    // `Accept-Encoding: identity` porque o Cloudflare comprime por padrao, e
    // um radares.bin em gzip chegaria com CRC que nao bate -- o firmware nao
    // descomprime e nao deve passar a descomprimir.
    char cabecalho[512];
    const int n_cab = std::snprintf(
        cabecalho, sizeof cabecalho,
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: coruja-gps\r\n"
        "Accept-Encoding: identity\r\n"
        "%s%s%s"
        "%s%u\r\n"
        "Connection: close\r\n"
        "\r\n",
        metodo, url.caminho, url.host,
        token != nullptr && token[0] != '\0' ? "X-Coruja-Token: " : "",
        token != nullptr && token[0] != '\0' ? token : "",
        token != nullptr && token[0] != '\0' ? "\r\n" : "",
        corpo > 0 ? "Content-Length: " : "Content-Length: ",
        static_cast<unsigned>(corpo));
    if (n_cab <= 0 || static_cast<std::size_t>(n_cab) >= sizeof cabecalho) {
        cyw43_arch_lwip_begin();
        altcp_arg(pcb, nullptr);
        altcp_abort(pcb);
        cyw43_arch_lwip_end();
        log.error(kOrigem, "cabecalho nao coube");
        saida.erro = ErroHttp::UrlInvalida;
        return saida;
    }

    const absolute_time_t prazo = make_timeout_time_ms(tempo_limite_ms);
    while (!pedido.terminou) {
        if (absolute_time_diff_us(get_absolute_time(), prazo) <= 0) {
            cyw43_arch_lwip_begin();
            altcp_arg(pcb, nullptr);
            altcp_abort(pcb);
            cyw43_arch_lwip_end();
            log.error(kOrigem, "tempo esgotado");
            saida.erro = ErroHttp::TempoEsgotado;
            saida.recebidos = pedido.leitor.recebidos();
            return saida;
        }
        if (pedido.conectou && !pedido.cabecalho_enviado) {
            cyw43_arch_lwip_begin();
            const bool ok =
                altcp_write(pcb, cabecalho, static_cast<u16_t>(n_cab),
                            TCP_WRITE_FLAG_COPY) == ERR_OK &&
                (pedido.corpo_total == 0 || bombeia(&pedido, pcb)) &&
                altcp_output(pcb) == ERR_OK;
            cyw43_arch_lwip_end();
            pedido.cabecalho_enviado = true;
            if (!ok) {
                cyw43_arch_lwip_begin();
                altcp_arg(pcb, nullptr);
                altcp_abort(pcb);
                cyw43_arch_lwip_end();
                log.error(kOrigem, pedido.fonte_secou
                                   ? "a fonte do corpo secou antes do fim"
                                   : "falha ao escrever o pedido");
                saida.erro = ErroHttp::Interrompida;
                return saida;
            }
        }
        cyw43_arch_poll();
        sleep_ms(1);
    }

    saida.recebidos = pedido.leitor.recebidos();
    saida.status = pedido.leitor.status();

    if (pedido.malformada) {
        log.error(kOrigem, "resposta malformada");
        saida.erro = ErroHttp::Interrompida;
        return saida;
    }
    if (pedido.falhou || !pedido.leitor.cabecalhos_prontos()) {
        // Sem cabecalhos nao houve resposta nenhuma -- e e aqui que um
        // certificado recusado chega, porque o handshake morre antes de
        // qualquer byte de HTTP.
        log.error(kOrigem, url.tls
                  ? "conexao interrompida (certificado? hora do aparelho?)"
                  : "conexao interrompida");
        saida.erro = ErroHttp::Interrompida;
        return saida;
    }
    if (saida.status < 200 || saida.status > 299) {
        std::snprintf(msg, sizeof msg, "status HTTP %u",
                      static_cast<unsigned>(saida.status));
        log.error(kOrigem, msg);
        saida.erro = ErroHttp::StatusNaoOk;
        return saida;
    }

    std::snprintf(msg, sizeof msg, "%u, %u bytes de corpo",
                  static_cast<unsigned>(saida.status),
                  static_cast<unsigned>(saida.recebidos));
    log.info(kOrigem, msg);
    return saida;
}

ResultadoHttp ClienteTls::baixa(const Url& url, AoReceber ao_receber,
                                void* contexto, Logger& log,
                                std::uint32_t tempo_limite_ms) {
    return conversa(&configuracao_, token_, url, "GET", nullptr, nullptr, 0,
                    ao_receber, contexto, log, tempo_limite_ms);
}

ResultadoHttp ClienteTls::envia(const Url& url, const char* token,
                                FonteDeBytes fonte, void* contexto,
                                std::size_t tamanho, Logger& log,
                                std::uint32_t tempo_limite_ms) {
    if (fonte == nullptr || tamanho == 0) {
        ResultadoHttp saida;
        saida.erro = ErroHttp::NaoIniciou;
        return saida;
    }
    return conversa(&configuracao_,
                    token != nullptr && token[0] != '\0' ? token : token_,
                    url, "PUT", fonte, contexto, tamanho, nullptr, nullptr,
                    log, tempo_limite_ms);
}

namespace {

/// Junta o corpo pequeno da consulta de CRC.
struct CorpoCurto {
    char        texto[32] = {};
    std::size_t n = 0;

    static void ao_receber(void* ctx, const std::uint8_t* b, std::size_t n) {
        auto* c = static_cast<CorpoCurto*>(ctx);
        const std::size_t livre = sizeof c->texto - 1 - c->n;
        const std::size_t quanto = n < livre ? n : livre;
        std::memcpy(c->texto + c->n, b, quanto);
        c->n += quanto;
        c->texto[c->n] = '\0';
    }
};

}  // namespace

ResultadoHttp ClienteTls::consulta_crc(const Url& url, const char* token,
                                       std::uint32_t* crc, Logger& log,
                                       std::uint32_t tempo_limite_ms) {
    CorpoCurto corpo;
    auto saida = conversa(&configuracao_,
                          token != nullptr && token[0] != '\0' ? token : token_,
                          url, "GET", nullptr, nullptr, 0,
                          CorpoCurto::ao_receber, &corpo, log,
                          tempo_limite_ms);
    if (!saida.ok()) { return saida; }

    if (!le_crc_hex(corpo.texto, corpo.n, crc)) {
        log.error(kOrigem, "resposta sem CRC legivel");
        saida.erro = ErroHttp::Interrompida;
    }
    return saida;
}

}  // namespace coruja
