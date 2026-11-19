#include "rede/RedeWifi.h"

#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include <cstdio>
#include <cstring>

#include "log/Logger.h"
#include "rede/CaixaVarredura.h"

namespace coruja {
namespace {

/// Quanto esperar a varredura terminar. O chip leva ~2 s para passar pelos
/// canais; 12 s dá folga para rádio lento sem travar o aparelho se o chip não
/// responder.
constexpr std::uint32_t kTempoLimiteVarreduraMs = 12'000;
constexpr std::uint32_t kTempoLimiteAssociacaoMs = 20'000;

/// A caixa desta unidade de tradução, e nada na pilha.
///
/// Vive pelo programa inteiro porque **não há como cancelar uma varredura**:
/// o driver segue chamando enquanto o rádio varre, inclusive depois de o
/// prazo estourar e de `conecta()` ter voltado. Ver `CaixaVarredura` — e
/// `CaixaDns`, que resolve a mesma classe de defeito para o DNS.
CaixaVarredura g_varredura;

/// Casca fina sobre a caixa: o `env` é o NÚMERO da geração, por valor, e não
/// um ponteiro para onde escrever.
int ao_ver_rede(void* env, const cyw43_ev_scan_result_t* r) {
    if (r == nullptr || r->ssid_len == 0) {
        return 0;
    }
    char ssid[kMaxSsid + 1] = {};
    const std::size_t n = r->ssid_len < kMaxSsid ? r->ssid_len : kMaxSsid;
    std::memcpy(ssid, r->ssid, n);

    const auto geracao =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(env));
    g_varredura.viu(geracao, ssid, static_cast<int>(r->rssi),
                    static_cast<unsigned>(r->channel));
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

    log.debug("wifi", "varrendo redes...");
    const std::uint32_t geracao = g_varredura.abre(cfg, log);
    cyw43_wifi_scan_options_t opcoes = {};
    if (cyw43_wifi_scan(&cyw43_state, &opcoes,
                        reinterpret_cast<void*>(
                            static_cast<std::uintptr_t>(geracao)),
                        ao_ver_rede) != 0) {
        g_varredura.abandona();
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

    // O que a varredura disser daqui em diante não interessa mais — e, se o
    // prazo estourou, ela AINDA ESTÁ RODANDO. Ler os números antes de
    // abandonar, e abandonar antes de qualquer retorno.
    const std::size_t escolhida = g_varredura.tem_escolhida()
                                      ? g_varredura.escolhida() : kMaxRedes;
    const std::size_t vistas = g_varredura.vistas();
    const int rssi_escolhida = g_varredura.rssi();
    const unsigned canal_escolhida = g_varredura.canal();
    g_varredura.abandona();

    char msg[128];
    std::snprintf(msg, sizeof msg, "varredura concluida: %u redes visiveis",
                  static_cast<unsigned>(vistas));
    log.debug("wifi", msg);

    if (escolhida >= cfg.n_redes) {
        // Uma linha, com os dois números que separam "a rede caiu" de "o
        // rádio não enxerga nada": quantas estavam configuradas e quantas
        // apareceram. Os SSID dos vizinhos não entram — eles não respondem
        // pergunta nenhuma e são de terceiros.
        std::snprintf(msg, sizeof msg,
                      "nenhuma das %u redes da lista entre as %u visiveis",
                      static_cast<unsigned>(cfg.n_redes),
                      static_cast<unsigned>(vistas));
        log.error("wifi", msg);
        return ErroWifi::NenhumaRedeVisivel;
    }

    // A partir daqui o prefixo da linha é sempre o mesmo: QUAL rede. O que
    // muda é o desfecho, depois do travessão.
    const Rede& rede = cfg.redes[escolhida];
    char alvo[80];
    std::snprintf(alvo, sizeof alvo, "'%s' (prio %u de %u, canal %u, %d dBm)",
                  rede.ssid, static_cast<unsigned>(escolhida + 1),
                  static_cast<unsigned>(cfg.n_redes), canal_escolhida,
                  rssi_escolhida);

    const std::uint32_t autenticacao =
        rede.aberta() ? CYW43_AUTH_OPEN : CYW43_AUTH_WPA2_AES_PSK;
    const int resultado = cyw43_arch_wifi_connect_timeout_ms(
        rede.ssid, rede.aberta() ? nullptr : rede.senha, autenticacao,
        kTempoLimiteAssociacaoMs);
    if (resultado != 0) {
        // Desassociar ANTES de voltar. O `connect_timeout_ms` desiste pelo
        // relogio dele, e o que ficou para tras -- autenticacao a meio
        // caminho, tentativa ainda em curso no chip -- nao e desfeito por
        // ninguem: o `desconecta()` so age quando houve associacao, e aqui
        // nao houve. O radio ficava consumindo e tentando sozinho.
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
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
