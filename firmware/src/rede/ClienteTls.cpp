#include "rede/ClienteTls.h"

#include <lwip/altcp.h>
#include <lwip/altcp_tcp.h>
#include <lwip/altcp_tls.h>
#include <lwip/dns.h>
#include <lwip/pbuf.h>
#include <lwip/stats.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include <cstdio>
#include <cstring>

#include "log/Logger.h"
#include "nucleo/TempoUtc.h"
#include "nucleo/Texto.h"
#include "rede/CaixaDns.h"
#include "rede/PlataformaMbedtls.h"
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
    /// Para o `ao_verificar` poder contar o que viu. O handshake e a UNICA
    /// janela: quando ele falha, o lwIP aborta e libera o pcb antes do
    /// `ao_falhar`, e depois disso nao sobra nada para interrogar.
    Logger* log = nullptr;

    bool conectou = false;
    bool terminou = false;
    bool falhou = false;
    bool malformada = false;
    bool fonte_secou = false;
};

/// Encerra o pedido. Devolve `true` se teve de **abortar** -- e ai quem chama
/// de dentro de um callback do lwIP precisa devolver `ERR_ABRT`.
///
/// O retorno do `altcp_close` nao pode ser ignorado. Quando ele falha, o
/// `altcp_mbedtls_close` REINSTALA os proprios callbacks no pcb interno e
/// devolve o erro: ficam vivos o pcb TLS e o `altcp_mbedtls_state_t`, que
/// carrega o `mbedtls_ssl_context` inteiro -- 16 KiB de buffer de entrada mais
/// 4 KiB de saida. Sao ~21 KiB por ocorrencia, num heap de ~158 KiB, e nada
/// mais aponta para eles.
///
/// Abortar resolve o vazamento e cria a segunda metade do problema: o contrato
/// do lwIP (`tcp.h`) diz que so se pode devolver `ERR_ABRT` **de dentro do
/// callback que abortou**. Devolver `ERR_OK` depois de abortar faz o
/// `tcp_input` seguir usando um pcb ja liberado. Por isso o aviso sobe.
bool encerra(Pedido* p, struct altcp_pcb* pcb, bool erro) {
    if (erro) { p->falhou = true; }
    p->terminou = true;
    if (pcb == nullptr) { return false; }
    altcp_arg(pcb, nullptr);
    altcp_sent(pcb, nullptr);
    altcp_recv(pcb, nullptr);
    altcp_err(pcb, nullptr);
    if (altcp_close(pcb) != ERR_OK) {
        altcp_abort(pcb);
        return true;
    }
    return false;
}

/// Empurra o que couber na janela. Volta quando ela fecha ou o corpo acaba.
bool bombeia(Pedido* p, struct altcp_pcb* pcb) {
    std::uint8_t pedaco[512];
    while (p->corpo_enviado < p->corpo_total) {
        const std::size_t janela = altcp_sndbuf(pcb);
        if (janela == 0) {
            // `altcp_output` ANTES de voltar. O `altcp_mbedtls_bio_send` so
            // chama `altcp_write` no pcb interno, nunca `altcp_output`: sem
            // isto os registros TLS ja cifrados ficam na fila de nao-enviados,
            // e o lwIP so os empurra quando chega um ACK -- que nao vem,
            // porque nada foi enviado. O envio para ate o prazo estourar.
            //
            // Quem chama do laco principal ja encadeia um `altcp_output`
            // logo em seguida; quem chama do callback `sent` nao, e e esse o
            // caminho que travava.
            //
            // O retorno e DESCARTADO de proposito, e nao por descuido: o
            // `tcp_output` devolve `ERR_MEM` quando nao consegue montar o
            // segmento AGORA, e isso e transitorio -- o lwIP tenta de novo
            // sozinho. Derrubar o envio inteiro por um soluco de alocacao
            // seria pior do que esperar, e quem segura o caso em que ele
            // nunca passa e o prazo, que existe para isso.
            static_cast<void>(altcp_output(pcb));
            return true;
        }

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
    if (!p->corpo_completo && !bombeia(p, pcb)) {
        if (encerra(p, pcb, true)) { return ERR_ABRT; }
    }
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
        return encerra(p, pcb, false) ? ERR_ABRT : ERR_OK;
    }

    const u16_t total = buf->tot_len;
    for (struct pbuf* parte = buf; parte != nullptr; parte = parte->next) {
        if (!p->leitor.alimenta(static_cast<const std::uint8_t*>(parte->payload),
                                parte->len, p->ao_receber, p->ctx_recebe)) {
            p->malformada = true;
            pbuf_free(buf);
            return encerra(p, pcb, true) ? ERR_ABRT : ERR_OK;
        }
    }

    altcp_recved(pcb, total);
    pbuf_free(buf);

    // Corpo completo pelo Content-Length ou pelo pedaco zero: nao ha o que
    // esperar. Fechar aqui evita segurar a conexao ate o tempo limite.
    if (p->leitor.completa()) {
        return encerra(p, pcb, false) ? ERR_ABRT : ERR_OK;
    }
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
    if (e != ERR_OK) { return encerra(p, pcb, true) ? ERR_ABRT : ERR_OK; }
    p->conectou = true;
    return ERR_OK;
}

/// A caixa do DNS deste cliente, e o endereco que ela autoriza copiar.
///
/// Vivem pelo programa inteiro porque o lwIP nao tem como cancelar um
/// `dns_gethostbyname`: ele guarda a funcao e o `arg` numa tabela e chama de
/// volta sempre, inclusive quando desiste. Um `arg` que fosse endereco de
/// variavel local viraria ponteiro para quadro de pilha ja reaproveitado.
///
/// Uma caixa por unidade de traducao, e nao uma compartilhada: o lwIP guarda
/// tambem o ponteiro da FUNCAO, entao a resposta atrasada de um pedido do NTP
/// so pode chegar ao callback do NTP, nunca a este.
CaixaDns g_dns;
ip_addr_t g_endereco_dns = {};

void ao_resolver(const char*, const ip_addr_t* ip, void* arg) {
    // `arg` NAO e ponteiro: e o numero da geracao, passado por valor. E assim
    // que a resposta atrasada deixa de ter para onde escrever.
    const auto geracao =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(arg));
    // Perguntar ANTES de copiar. Na ordem inversa, uma resposta atrasada
    // escreveria por cima do endereco de um pedido novo ja resolvido.
    if (g_dns.entrega(geracao, ip != nullptr) && ip != nullptr) {
        g_endereco_dns = *ip;
    }
}

/// Buffers de ARQUIVO, nao de pilha, de proposito.
///
/// O `ao_verificar` roda no ponto mais fundo da pilha do programa: dentro do
/// handshake do mbedTLS, dentro do callback de recepcao do lwIP, dentro do
/// `cyw43_arch_poll()`. Meio kibibyte de locais aqui e meio kibibyte tirado de
/// onde ja e mais apertado -- a pilha inteira tem 8 KiB. Nao ha reentrancia: o
/// cyw43 esta em modo poll e so existe um pedido por vez.
char g_nome_cert[80];
char g_msg_cert[192];
char g_porque_cert[112];

/// Chamado pelo mbedTLS para CADA certificado da cadeia, durante o handshake.
///
/// Existe por uma razao so: quando o handshake falha, o lwIP aborta e libera o
/// pcb antes do `ao_falhar`, e depois disso nao sobra nada para interrogar.
/// Sem isto, toda falha de TLS volta como "conexao interrompida" -- um palpite
/// com duas hipoteses, que foi exatamente o que os testes em bancada
/// devolveram. Esta e a unica janela em que da para saber QUAL certificado
/// caiu e POR QUE.
///
/// Devolve sempre zero: nao altera o veredito, so conta o que aconteceu. Quem
/// recusa continua sendo o `MBEDTLS_SSL_VERIFY_REQUIRED` do `lwipopts.h`.
///
/// O certificado que PASSA sai em `info`, que o `LoggerCartao` so acumula --
/// nenhum toque no cartao durante um handshake que vai dar certo. O que e
/// RECUSADO sai em `error`, e isso descarrega no cartao ali mesmo, no meio do
/// handshake. E deliberado: quando esta linha existe o handshake ja esta
/// perdido (o `VERIFY_REQUIRED` vai derruba-lo assim que esta funcao voltar),
/// entao a escrita nao atrapalha conexao nenhuma que fosse vingar -- e e
/// exatamente a linha que precisa sobreviver ao travamento.
int ao_verificar(void* ctx, mbedtls_x509_crt* crt, int profundidade,
                 std::uint32_t* bandeiras) {
    auto* p = static_cast<Pedido*>(ctx);
    if (p == nullptr || p->log == nullptr || crt == nullptr ||
        bandeiras == nullptr) {
        return 0;
    }
    if (mbedtls_x509_dn_gets(g_nome_cert, sizeof g_nome_cert,
                             &crt->subject) < 0) {
        std::snprintf(g_nome_cert, sizeof g_nome_cert, "(nome ilegivel)");
    }

    if (*bandeiras == 0) {
        std::snprintf(g_msg_cert, sizeof g_msg_cert, "cert %d ok: %s",
                      profundidade, g_nome_cert);
        p->log->info(kOrigem, g_msg_cert);
        return 0;
    }

    g_porque_cert[0] = '\0';
    mbedtls_x509_crt_verify_info(g_porque_cert, sizeof g_porque_cert, "",
                                 *bandeiras);
    // O `verify_info` separa os motivos com `\n`, e o log grava uma linha por
    // chamada: sem isto a mensagem sai quebrada no meio do arquivo.
    for (char* c = g_porque_cert; *c != '\0'; ++c) {
        if (*c == '\n' || *c == '\r') { *c = ' '; }
    }
    std::snprintf(g_msg_cert, sizeof g_msg_cert,
                  "cert %d RECUSADO (0x%08lx) %s: %s", profundidade,
                  static_cast<unsigned long>(*bandeiras), g_nome_cert,
                  g_porque_cert);
    p->log->error(kOrigem, g_msg_cert);
    return 0;
}

bool resolve(const char* host, ip_addr_t* destino, std::uint32_t limite_ms,
             Logger& log) {
    // `cache` e local e isso esta certo: o lwIP so escreve nele de forma
    // SINCRONA, quando o nome ja esta em cache, e nunca o guarda para depois.
    // Quem ele guarda e o `callback_arg`, e esse e que deixou de ser ponteiro.
    ip_addr_t cache = {};
    const std::uint32_t geracao = g_dns.abre();
    cyw43_arch_lwip_begin();
    const err_t imediato = dns_gethostbyname(
        host, &cache, ao_resolver,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(geracao)));
    cyw43_arch_lwip_end();

    if (imediato == ERR_OK) {
        g_dns.abandona();
        *destino = cache;
        return true;
    }
    if (imediato != ERR_INPROGRESS) {
        g_dns.abandona();
        log.error(kOrigem, "DNS nao iniciou");
        return false;
    }
    const absolute_time_t prazo = make_timeout_time_ms(limite_ms);
    while (!g_dns.pronto()) {
        if (absolute_time_diff_us(get_absolute_time(), prazo) <= 0) {
            // O lwIP VAI chamar de volta, mais tarde, com esta geracao --
            // nao ha como cancelar. Abandonar e o que faz essa chamada nao
            // encontrar ninguem.
            g_dns.abandona();
            log.error(kOrigem, "DNS nao respondeu a tempo");
            return false;
        }
        cyw43_arch_poll();
        sleep_ms(1);
    }
    const bool achou = g_dns.achou();
    g_dns.abandona();
    if (!achou) {
        log.error(kOrigem, "host nao resolveu");
        return false;
    }
    *destino = g_endereco_dns;
    return true;
}

}  // namespace

ClienteTls::~ClienteTls() { libera_configuracao(); }

bool ClienteTls::pico_do_heap(std::size_t* usado, std::size_t* total) {
#if MEM_STATS
    if (usado == nullptr || total == nullptr) { return false; }
    *usado = lwip_stats.mem.max;
    *total = lwip_stats.mem.avail;
    return true;
#else
    static_cast<void>(usado);
    static_cast<void>(total);
    return false;
#endif
}

bool ClienteTls::define_token(const char* token) {
    if (token != nullptr && !seguro_para_cabecalho(token)) {
        token_[0] = '\0';
        return false;
    }
    std::snprintf(token_, sizeof token_, "%s", token == nullptr ? "" : token);
    return true;
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
    pedido.log = &log;
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
        if (ssl != nullptr) {
            // Por sessao, e nao na configuracao: a configuracao e do lwIP e
            // nao tem gancho publico. `mbedtls_ssl_set_verify` tem precedencia
            // sobre o `conf_verify`, entao e exatamente este que roda.
            //
            // `&pedido` e local, e aqui isso e seguro: o contexto de SSL vive
            // dentro do pcb, e TODA saida desta funcao abaixo deste ponto
            // fecha ou aborta o pcb antes de voltar. Nao e o caso do DNS, e e
            // por isso que la a caixa e estatica.
            mbedtls_ssl_set_verify(ssl, ao_verificar, &pedido);
        }
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
    //
    // `Content-Length` so quando ha corpo. O que havia aqui era um ternario
    // com os DOIS RAMOS IGUAIS -- a intencao de omitir estava escrita e nunca
    // acontecia, e todo GET saia com `Content-Length: 0`. Legal, mas e codigo
    // morto dizendo uma coisa e fazendo outra.
    char tamanho[40] = {};
    if (corpo > 0) {
        std::snprintf(tamanho, sizeof tamanho, "Content-Length: %u\r\n",
                      static_cast<unsigned>(corpo));
    }

    char cabecalho[512];
    const int n_cab = std::snprintf(
        cabecalho, sizeof cabecalho,
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: coruja-gps\r\n"
        "Accept-Encoding: identity\r\n"
        "%s%s%s"
        "%s"
        "Connection: close\r\n"
        "\r\n",
        metodo, url.caminho, url.host,
        token != nullptr && token[0] != '\0' ? "X-Coruja-Token: " : "",
        token != nullptr && token[0] != '\0' ? token : "",
        token != nullptr && token[0] != '\0' ? "\r\n" : "",
        tamanho);
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
        //
        // O QUE houve com o certificado ja saiu pelo `ao_verificar`, que roda
        // durante o handshake. O que falta e a outra hipotese, e ela custa
        // zero para medir: registrar a hora que o aparelho acredita ser. Ou o
        // numero esta em 2026, ou esta em 1970 -- e no segundo caso todo
        // certificado valido e recusado por "ainda nao comecou a valer".
        if (url.tls) {
            const std::int64_t agora = hora_utc();
            const TempoUtc t = de_segundos(agora);
            std::snprintf(msg, sizeof msg,
                          "TLS interrompido; o aparelho acredita ser "
                          "%04u-%02u-%02u %02u:%02u:%02uZ%s",
                          static_cast<unsigned>(t.ano),
                          static_cast<unsigned>(t.mes),
                          static_cast<unsigned>(t.dia),
                          static_cast<unsigned>(t.hora),
                          static_cast<unsigned>(t.minuto),
                          static_cast<unsigned>(t.segundo),
                          agora == 0 ? " -- RELOGIO NAO ACERTADO" : "");
            log.error(kOrigem, msg);
        } else {
            log.error(kOrigem, "conexao interrompida");
        }
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
