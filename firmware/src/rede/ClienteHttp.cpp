#include "rede/ClienteHttp.h"

#include <lwip/altcp.h>
#include <lwip/apps/http_client.h>
#include <lwip/pbuf.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include <cstdio>

#include "log/Logger.h"

namespace coruja {
namespace {

/// Estado de uma transferência, compartilhado com os callbacks em C do lwIP.
///
/// Este firmware usa o `cyw43_arch` em modo **poll**: os callbacks rodam no
/// mesmo contexto do laço principal, chamados de dentro de `cyw43_arch_poll()`.
/// Não há concorrência com interrupção aqui, e é por isso que os campos não
/// precisam de proteção — o modo background, que roda em IRQ, precisaria.
struct Transferencia {
    ClienteHttp::AoReceber ao_receber = nullptr;
    void*                  contexto = nullptr;
    std::size_t            recebidos = 0;
    std::uint32_t          status = 0;
    httpc_result_t         resultado = HTTPC_RESULT_OK;
    bool                   terminou = false;
};

err_t ao_chegar_corpo(void* arg, struct altcp_pcb* conexao, struct pbuf* p,
                      err_t erro) {
    auto* t = static_cast<Transferencia*>(arg);
    if (p == nullptr) {
        return ERR_OK;  // fim da conexão; o result_fn dá o veredito
    }
    if (erro != ERR_OK) {
        pbuf_free(p);
        return erro;
    }

    // O pbuf é uma CADEIA, não um bloco. Percorrer só o primeiro elo perderia
    // bytes silenciosamente — e o sintoma seria um CRC errado no fim de um
    // download que pareceu completo.
    for (struct pbuf* parte = p; parte != nullptr; parte = parte->next) {
        if (parte->len > 0 && t->ao_receber != nullptr) {
            t->ao_receber(t->contexto,
                          static_cast<const std::uint8_t*>(parte->payload),
                          parte->len);
        }
        t->recebidos += parte->len;
    }

    altcp_recved(conexao, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

err_t ao_chegar_cabecalhos(httpc_state_t*, void* arg, struct pbuf*, u16_t,
                           u32_t) {
    static_cast<void>(arg);
    return ERR_OK;
}

void ao_terminar(void* arg, httpc_result_t resultado, u32_t recebidos,
                 u32_t status, err_t) {
    auto* t = static_cast<Transferencia*>(arg);
    t->resultado = resultado;
    t->status = status;
    if (recebidos > t->recebidos) {
        t->recebidos = recebidos;
    }
    t->terminou = true;
}

}  // namespace

const char* descreve(ErroHttp erro) {
    switch (erro) {
        case ErroHttp::Nenhum:          return "ok";
        case ErroHttp::UrlInvalida:     return "URL invalida";
        case ErroHttp::TlsNaoSuportado: return "https: este cliente nao fala TLS";
        case ErroHttp::NaoIniciou:      return "o pedido nao saiu (DNS? memoria?)";
        case ErroHttp::TempoEsgotado:   return "tempo esgotado";
        case ErroHttp::StatusNaoOk:     return "servidor respondeu com status != 200";
        case ErroHttp::Interrompida:    return "conexao interrompida no meio";
    }
    return "erro desconhecido";
}

ResultadoHttp ClienteHttp::baixa(const Url& url, AoReceber ao_receber,
                                 void* contexto, Logger& log,
                                 std::uint32_t tempo_limite_ms) {
    ResultadoHttp saida;

    if (url.tls) {
        log.error("http", "URL https: este cliente nao fala TLS (RF05.2)");
        saida.erro = ErroHttp::TlsNaoSuportado;
        return saida;
    }
    if (url.host[0] == '\0') {
        saida.erro = ErroHttp::UrlInvalida;
        return saida;
    }

    Transferencia transferencia;
    transferencia.ao_receber = ao_receber;
    transferencia.contexto = contexto;

    httpc_connection_t ajustes = {};
    ajustes.result_fn = ao_terminar;
    ajustes.headers_done_fn = ao_chegar_cabecalhos;

    char msg[224];
    std::snprintf(msg, sizeof msg, "GET http://%s:%u%s", url.host,
                  static_cast<unsigned>(url.porta), url.caminho);
    log.info("http", msg);

    httpc_state_t* conexao = nullptr;
    const err_t partida = httpc_get_file_dns(
        url.host, url.porta, url.caminho, &ajustes, ao_chegar_corpo,
        &transferencia, &conexao);
    if (partida != ERR_OK) {
        std::snprintf(msg, sizeof msg, "o pedido nao saiu (lwip err %d)",
                      static_cast<int>(partida));
        log.error("http", msg);
        saida.erro = ErroHttp::NaoIniciou;
        return saida;
    }

    const absolute_time_t limite = make_timeout_time_ms(tempo_limite_ms);
    while (!transferencia.terminou) {
        if (absolute_time_diff_us(get_absolute_time(), limite) <= 0) {
            log.error("http", "tempo esgotado esperando a resposta");
            saida.erro = ErroHttp::TempoEsgotado;
            saida.recebidos = transferencia.recebidos;
            return saida;
        }
        cyw43_arch_poll();
        sleep_ms(1);
    }

    saida.status = transferencia.status;
    saida.recebidos = transferencia.recebidos;

    if (transferencia.resultado != HTTPC_RESULT_OK) {
        std::snprintf(msg, sizeof msg,
                      "transferencia interrompida (httpc %d, status %u, %u B)",
                      static_cast<int>(transferencia.resultado),
                      static_cast<unsigned>(saida.status),
                      static_cast<unsigned>(saida.recebidos));
        log.error("http", msg);
        saida.erro = ErroHttp::Interrompida;
        return saida;
    }
    if (saida.status != 200) {
        // O 503 do servidor de referência cai aqui, e é informação útil: a
        // rota existe, a base é que não foi publicada.
        std::snprintf(msg, sizeof msg, "status HTTP %u",
                      static_cast<unsigned>(saida.status));
        log.error("http", msg);
        saida.erro = ErroHttp::StatusNaoOk;
        return saida;
    }

    std::snprintf(msg, sizeof msg, "200 OK, %u bytes de corpo",
                  static_cast<unsigned>(saida.recebidos));
    log.info("http", msg);
    return saida;
}

}  // namespace coruja
