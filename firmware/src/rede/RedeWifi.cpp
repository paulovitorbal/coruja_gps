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
            }
            char msg[96];
            std::snprintf(msg, sizeof msg,
                          "  %-32s canal %2u  %4d dBm  <- da lista (prio %u)",
                          ssid, static_cast<unsigned>(r->channel),
                          static_cast<int>(r->rssi),
                          static_cast<unsigned>(i + 1));
            coleta->log->info("wifi", msg);
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

const char* descreve(ErroWifi erro) {
    switch (erro) {
        case ErroWifi::Nenhum:                 return "ok";
        case ErroWifi::ChipNaoIniciou:         return "chip CYW43 nao iniciou";
        case ErroWifi::VarreduraFalhou:        return "varredura falhou";
        case ErroWifi::NenhumaRedeConfigurada: return "nenhuma rede na configuracao";
        case ErroWifi::NenhumaRedeVisivel:     return "nenhuma rede da lista esta visivel";
        case ErroWifi::FalhaDeAssociacao:      return "associacao recusada (senha? sinal?)";
    }
    return "erro desconhecido";
}

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
    log.info("wifi", "chip CYW43 iniciado, modo estacao");
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

    log.info("wifi", "varrendo redes...");
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

    char msg[96];
    std::snprintf(msg, sizeof msg, "varredura concluida: %u redes visiveis",
                  static_cast<unsigned>(coleta.vistas));
    log.info("wifi", msg);

    if (coleta.escolhida >= cfg.n_redes) {
        return ErroWifi::NenhumaRedeVisivel;
    }

    const Rede& rede = cfg.redes[coleta.escolhida];
    std::snprintf(msg, sizeof msg, "conectando em '%s' (prioridade %u de %u)",
                  rede.ssid, static_cast<unsigned>(coleta.escolhida + 1),
                  static_cast<unsigned>(cfg.n_redes));
    log.info("wifi", msg);

    const std::uint32_t autenticacao =
        rede.aberta() ? CYW43_AUTH_OPEN : CYW43_AUTH_WPA2_AES_PSK;
    const int resultado = cyw43_arch_wifi_connect_timeout_ms(
        rede.ssid, rede.aberta() ? nullptr : rede.senha, autenticacao,
        kTempoLimiteAssociacaoMs);
    if (resultado != 0) {
        // Nunca a senha, nem em falha: um log de bancada vira captura de tela.
        std::snprintf(msg, sizeof msg, "associacao falhou (codigo %d)", resultado);
        log.error("wifi", msg);
        return ErroWifi::FalhaDeAssociacao;
    }

    associado_ = true;
    const std::uint32_t ip = cyw43_state.netif[CYW43_ITF_STA].ip_addr.addr;
    std::snprintf(msg, sizeof msg, "conectado. IP %u.%u.%u.%u",
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
    log.info("wifi", "desconectado (chip segue ligado para a proxima vez)");
}

}  // namespace coruja
