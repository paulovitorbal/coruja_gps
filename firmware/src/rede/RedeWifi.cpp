#include "rede/RedeWifi.h"

#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include <cstdio>
#include <cstring>

#include "log/Logger.h"

namespace coruja {
namespace {

/// Quanto esperar a varredura terminar. O chip leva ~2 s para passar pelos
/// canais; 12 s dá folga para rádio lento sem travar o aparelho se o chip não
/// responder.
constexpr std::uint32_t kTempoLimiteVarreduraMs = 12'000;
constexpr std::uint32_t kTempoLimiteAssociacaoMs = 20'000;

/// O que a varredura encontrou. Passado como `env` ao callback do driver, que
/// é C e não conhece captura de lambda.
struct Coleta {
    const Configuracao* cfg = nullptr;
    Logger*             log = nullptr;
    /// Índice, na lista da configuração, da rede visível de MAIOR prioridade.
    /// `kMaxRedes` significa "nenhuma".
    std::size_t         escolhida = kMaxRedes;
    std::size_t         vistas = 0;
    /// Sinal e canal **da escolhida**, para a linha única do desfecho.
    ///
    /// 🔴 **A varredura despejava uma linha por rede** (27/10/2026): 284
    /// linhas e 17 KB no histórico de 108 partidas, listando o Wi-Fi dos
    /// vizinhos num arquivo que sobe pela rede. O que a remessa precisa
    /// responder é "em qual tentou e deu certo?" — e isso é uma linha.
    int                 rssi_escolhida = 0;
    unsigned            canal_escolhida = 0;
};

int ao_ver_rede(void* env, const cyw43_ev_scan_result_t* r) {
    auto* coleta = static_cast<Coleta*>(env);
    if (r == nullptr || r->ssid_len == 0) {
        return 0;
    }

    char ssid[kMaxSsid + 1] = {};
    const std::size_t n = r->ssid_len < kMaxSsid ? r->ssid_len : kMaxSsid;
    std::memcpy(ssid, r->ssid, n);

    ++coleta->vistas;

    // Procura este SSID na lista, e guarda a MENOR posição encontrada: a
    // varredura devolve os pontos de acesso em ordem arbitrária, então não dá
    // para parar no primeiro que casar.
    for (std::size_t i = 0; i < coleta->cfg->n_redes; ++i) {
        if (std::strcmp(coleta->cfg->redes[i].ssid, ssid) == 0) {
            if (i < coleta->escolhida) {
                coleta->escolhida = i;
                coleta->rssi_escolhida = static_cast<int>(r->rssi);
                coleta->canal_escolhida = static_cast<unsigned>(r->channel);
            }
            char msg[96];
            std::snprintf(msg, sizeof msg,
                          "  %-32s canal %2u  %4d dBm  <- da lista (prio %u)",
                          ssid, static_cast<unsigned>(r->channel),
                          static_cast<int>(r->rssi),
                          static_cast<unsigned>(i + 1));
            coleta->log->debug("wifi", msg);
            return 0;
        }
    }

    char msg[96];
    std::snprintf(msg, sizeof msg, "  %-32s canal %2u  %4d dBm", ssid,
                  static_cast<unsigned>(r->channel), static_cast<int>(r->rssi));
    coleta->log->debug("wifi", msg);
    return 0;
}

}  // namespace


RedeWifi::~RedeWifi() {
    if (iniciado_) {
        cyw43_arch_deinit();
    }
}

ErroWifi RedeWifi::inicia(Logger& log) {
    if (iniciado_) {
        return ErroWifi::Nenhum;
    }
    if (cyw43_arch_init() != 0) {
        log.error("wifi", "cyw43_arch_init falhou");
        return ErroWifi::ChipNaoIniciou;
    }
    cyw43_arch_enable_sta_mode();
    iniciado_ = true;
    log.debug("wifi", "chip CYW43 iniciado, modo estacao");
    return ErroWifi::Nenhum;
}

ErroWifi RedeWifi::conecta(const Configuracao& cfg, Logger& log) {
    if (!iniciado_) {
        const auto erro = inicia(log);
        if (erro != ErroWifi::Nenhum) {
            return erro;
        }
    }
    if (!cfg.tem_rede()) {
        return ErroWifi::NenhumaRedeConfigurada;
    }

    Coleta coleta;
    coleta.cfg = &cfg;
    coleta.log = &log;

    log.debug("wifi", "varrendo redes...");
    cyw43_wifi_scan_options_t opcoes = {};
    if (cyw43_wifi_scan(&cyw43_state, &opcoes, &coleta, ao_ver_rede) != 0) {
        return ErroWifi::VarreduraFalhou;
    }

    const absolute_time_t limite =
        make_timeout_time_ms(kTempoLimiteVarreduraMs);
    while (cyw43_wifi_scan_active(&cyw43_state)) {
        if (absolute_time_diff_us(get_absolute_time(), limite) <= 0) {
            log.warning("wifi", "varredura estourou o tempo limite");
            break;
        }
        // `sleep_ms(1)` e não espera ocupada: o stdio USB depende de um alarme
        // para chamar `tud_task()`, e um laço fechado o emudece. Foi o que
        // derrubou duas versões do diagnóstico do encoder.
        cyw43_arch_poll();
        sleep_ms(1);
    }

    char msg[128];
    std::snprintf(msg, sizeof msg, "varredura concluida: %u redes visiveis",
                  static_cast<unsigned>(coleta.vistas));
    log.debug("wifi", msg);

    if (coleta.escolhida >= cfg.n_redes) {
        // Uma linha, com os dois números que separam "a rede caiu" de "o
        // rádio não enxerga nada": quantas estavam configuradas e quantas
        // apareceram. Os SSID dos vizinhos não entram — eles não respondem
        // pergunta nenhuma e são de terceiros.
        std::snprintf(msg, sizeof msg,
                      "nenhuma das %u redes da lista entre as %u visiveis",
                      static_cast<unsigned>(cfg.n_redes),
                      static_cast<unsigned>(coleta.vistas));
        log.error("wifi", msg);
        return ErroWifi::NenhumaRedeVisivel;
    }

    // A partir daqui o prefixo da linha é sempre o mesmo: QUAL rede. O que
    // muda é o desfecho, depois do travessão.
    const Rede& rede = cfg.redes[coleta.escolhida];
    char alvo[80];
    std::snprintf(alvo, sizeof alvo, "'%s' (prio %u de %u, canal %u, %d dBm)",
                  rede.ssid, static_cast<unsigned>(coleta.escolhida + 1),
                  static_cast<unsigned>(cfg.n_redes), coleta.canal_escolhida,
                  coleta.rssi_escolhida);

    const std::uint32_t autenticacao =
        rede.aberta() ? CYW43_AUTH_OPEN : CYW43_AUTH_WPA2_AES_PSK;
    const int resultado = cyw43_arch_wifi_connect_timeout_ms(
        rede.ssid, rede.aberta() ? nullptr : rede.senha, autenticacao,
        kTempoLimiteAssociacaoMs);
    if (resultado != 0) {
        // Nunca a senha, nem em falha: um log de bancada vira captura de tela.
        std::snprintf(msg, sizeof msg, "%s - associacao falhou (codigo %d)",
                      alvo, resultado);
        log.error("wifi", msg);
        return ErroWifi::FalhaDeAssociacao;
    }

    associado_ = true;
    const std::uint32_t ip = cyw43_state.netif[CYW43_ITF_STA].ip_addr.addr;
    std::snprintf(msg, sizeof msg, "%s - conectado, IP %u.%u.%u.%u", alvo,
                  static_cast<unsigned>(ip & 0xFFU),
                  static_cast<unsigned>((ip >> 8) & 0xFFU),
                  static_cast<unsigned>((ip >> 16) & 0xFFU),
                  static_cast<unsigned>((ip >> 24) & 0xFFU));
    log.info("wifi", msg);
    return ErroWifi::Nenhum;
}

void RedeWifi::desconecta(Logger& log) {
    if (!iniciado_ || !associado_) {
        return;
    }
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    associado_ = false;
    log.debug("wifi", "desconectado (chip segue ligado para a proxima vez)");
}

}  // namespace coruja
