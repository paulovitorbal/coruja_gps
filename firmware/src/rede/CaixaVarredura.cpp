#include "rede/CaixaVarredura.h"

#include <cstdio>
#include <cstring>

#include "log/Logger.h"

namespace coruja {

std::uint32_t CaixaVarredura::abre(const Configuracao& cfg, Logger& log) {
    ++ultima_;
    // `ultima_` so cresce. Zera-la ao abandonar faria a geracao seguinte
    // repetir uma ja usada, e uma chamada atrasada daquela seria aceita como
    // sendo desta -- exatamente o que a geracao existe para impedir.
    if (ultima_ == 0) { ultima_ = 1; }
    em_curso_ = ultima_;
    cfg_ = &cfg;
    log_ = &log;
    escolhida_ = kMaxRedes;
    vistas_ = 0;
    rssi_ = 0;
    canal_ = 0;
    return em_curso_;
}

void CaixaVarredura::abandona() {
    em_curso_ = 0;
    // Os dois ponteiros saem junto: depois daqui a caixa nao tem como tocar
    // em nada que fosse de quem ja foi embora.
    cfg_ = nullptr;
    log_ = nullptr;
}

void CaixaVarredura::viu(std::uint32_t geracao, const char* ssid, int rssi,
                         unsigned canal) {
    if (geracao == 0 || geracao != em_curso_ || cfg_ == nullptr) { return; }
    // Rede oculta: o ponto de acesso responde sem o nome, e nao ha o que
    // comparar com a lista.
    if (ssid == nullptr || ssid[0] == '\0') { return; }

    ++vistas_;

    char msg[96];
    for (std::size_t i = 0; i < cfg_->n_redes; ++i) {
        if (std::strcmp(cfg_->redes[i].ssid, ssid) != 0) { continue; }
        // A MENOR posicao encontrada, e nao a primeira que casar: a
        // varredura devolve os pontos de acesso em ordem arbitraria.
        if (i < escolhida_) {
            escolhida_ = i;
            rssi_ = rssi;
            canal_ = canal;
        }
        if (log_ != nullptr) {
            std::snprintf(msg, sizeof msg,
                          "  %-32s canal %2u  %4d dBm  <- da lista (prio %u)",
                          ssid, canal, rssi, static_cast<unsigned>(i + 1));
            log_->debug("wifi", msg);
        }
        return;
    }

    if (log_ != nullptr) {
        // DEBUG, e so: o SSID do vizinho e de terceiros, e o log sobe pela
        // rede. Eram 284 linhas e 17 KB no historico de 108 partidas.
        std::snprintf(msg, sizeof msg, "  %-32s canal %2u  %4d dBm", ssid,
                      canal, rssi);
        log_->debug("wifi", msg);
    }
}

}  // namespace coruja
