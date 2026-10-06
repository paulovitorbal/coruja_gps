#include "nucleo/SincronizadorHora.h"

#include <cstdio>

#include "log/Logger.h"

namespace coruja {
namespace {

constexpr const char* kOrigem = "hora";

}  // namespace

const char* descreve(OrigemHora o) {
    switch (o) {
        case OrigemHora::Nenhuma:    return "nenhuma";
        case OrigemHora::JaAjustado: return "ja ajustado";
        case OrigemHora::Ntp:        return "ntp";
        case OrigemHora::Gps:        return "gps";
    }
    return "desconhecida";
}

bool SincronizadorHora::aceita(std::int64_t segundos, OrigemHora origem,
                               Logger& log) {
    const TempoUtc t = de_segundos(segundos);
    if (!t.plausivel()) {
        char msg[96];
        std::snprintf(msg, sizeof msg, "%s devolveu hora implausivel",
                      descreve(origem));
        log.warning(kOrigem, msg);
        return false;
    }
    relogio_.define_utc(segundos);
    ja_sincronizado_ = true;

    char msg[96];
    std::snprintf(msg, sizeof msg,
                  "relogio acertado por %s: %04u-%02u-%02u %02u:%02u:%02uZ",
                  descreve(origem), static_cast<unsigned>(t.ano),
                  static_cast<unsigned>(t.mes), static_cast<unsigned>(t.dia),
                  static_cast<unsigned>(t.hora), static_cast<unsigned>(t.minuto),
                  static_cast<unsigned>(t.segundo));
    log.info(kOrigem, msg);
    return true;
}

OrigemHora SincronizadorHora::sincroniza(const char* servidor,
                                         const Telemetria& gps, Logger& log) {
    // Ja acertado NESTA ligacao: nao se mexe. Reconsultar a cada OTA gastaria
    // rede e, pior, daria a quem controla a rede uma segunda chance de mover
    // um relogio que ja estava bom.
    if (ja_sincronizado_ && de_segundos(relogio_.agora_utc()).plausivel()) {
        log.debug(kOrigem, "relogio ja ajustado nesta ligacao");
        return OrigemHora::JaAjustado;
    }

    // --- NTP primeiro: ver o comentario da classe ---
    const char* alvo = (servidor != nullptr && servidor[0] != '\0')
                       ? servidor : kServidorNtpPadrao;
    std::int64_t segundos = 0;
    if (ntp_.consulta(alvo, &segundos, log) &&
            aceita(segundos, OrigemHora::Ntp, log)) {
        return OrigemHora::Ntp;
    }

    // --- GPS como alternativa, SEM ESPERAR por fix ---
    //
    // So vale o que a telemetria ja trouxe. Ficar aqui esperando o receptor
    // pegar sinal seria reintroduzir exatamente a espera que a ordem acima
    // existe para evitar -- e numa garagem ela nao termina.
    if (gps.data_valida) {
        const TempoUtc t{gps.ano, gps.mes, gps.dia,
                         gps.hora, gps.minuto, gps.segundo};
        if (t.plausivel() && aceita(segundos_de(t), OrigemHora::Gps, log)) {
            return OrigemHora::Gps;
        }
    }

    log.error(kOrigem, "sem hora: nem NTP nem GPS; TLS nao vai validar data");
    return OrigemHora::Nenhuma;
}

}  // namespace coruja
