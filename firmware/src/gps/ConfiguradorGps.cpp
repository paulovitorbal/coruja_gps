#include "gps/ConfiguradorGps.h"

#include <cstdio>

#include "log/Logger.h"

namespace coruja {

const char* descreve(ResultadoConfigGps r) {
    switch (r) {
        case ResultadoConfigGps::Configurado:  return "configurado";
        case ResultadoConfigGps::SemResposta:  return "o modulo nao respondeu";
        case ResultadoConfigGps::Recusado:     return "o modulo recusou (NAK)";
        case ResultadoConfigGps::NaoPersistiu: return "configurou sem persistir";
    }
    return "?";
}

bool ConfiguradorGps::espera_ack(std::uint8_t id, Logger& log, bool* recusou) {
    *recusou = false;
    std::uint8_t bloco[128];
    for (unsigned volta = 0; volta < kEsperasPorAck; ++volta) {
        const std::size_t lidos = uart_.le(bloco, sizeof bloco);
        for (std::size_t i = 0; i < lidos; ++i) {
            const auto evento = leitor_.consome(bloco[i]);
            // Filtra pelo id: chega ACK de outros comandos e, entre eles,
            // sentenças NMEA inteiras. Aceitar qualquer ACK daria por
            // confirmado um comando que o módulo nem viu.
            if (evento == ubx::EventoUbx::Ack &&
                leitor_.classe_alvo() == ubx::kClasseCfg &&
                leitor_.id_alvo() == id) {
                return true;
            }
            if (evento == ubx::EventoUbx::Nak &&
                leitor_.classe_alvo() == ubx::kClasseCfg &&
                leitor_.id_alvo() == id) {
                *recusou = true;
                return false;
            }
        }
        if (lidos == 0) {
            pausa_.espera_ms(kEsperaEntreLeiturasMs);
        }
    }
    return false;
}

ConfiguradorGps::Passo ConfiguradorGps::comanda(const std::uint8_t* quadro,
                                                std::size_t tamanho,
                                                std::uint8_t id,
                                                const char* nome, Logger& log) {
    char msg[128];
    for (unsigned tentativa = 1; tentativa <= kTentativasPorComando;
         ++tentativa) {
        uart_.escreve(quadro, tamanho);
        bool recusou = false;
        if (espera_ack(id, log, &recusou)) {
            std::snprintf(msg, sizeof msg, "%s: ACK", nome);
            log.info("gps", msg);
            return Passo::Ok;
        }
        if (recusou) {
            // NAK não melhora repetindo: o módulo entendeu e recusou o
            // conteúdo. Insistir só atrasaria o diagnóstico.
            std::snprintf(msg, sizeof msg, "%s: NAK, o modulo recusou", nome);
            log.error("gps", msg);
            return Passo::Nak;
        }
        std::snprintf(msg, sizeof msg, "%s: sem resposta (tentativa %u de %u)",
                      nome, tentativa, kTentativasPorComando);
        log.warning("gps", msg);
    }
    return Passo::Silencio;
}

ResultadoConfigGps ConfiguradorGps::executa(Logger& log) {
    std::uint8_t q[64];
    log.info("gps", "==== configurando o receptor (RF01.2) ====");

    // --- 1. taxa de navegação, ainda no baud de fábrica -------------------
    std::size_t n = ubx::monta_cfg_rate(kPeriodoNavegacaoMs, q, sizeof q);
    switch (comanda(q, n, ubx::kCfgRate, "CFG-RATE 250 ms", log)) {
        case Passo::Ok:       break;
        case Passo::Nak:      return ResultadoConfigGps::Recusado;
        case Passo::Silencio: return ResultadoConfigGps::SemResposta;
    }

    // --- 2. só a RMC na porta ---------------------------------------------
    //
    // Desligar ANTES de subir o baud é de propósito: a 9600 as sete sentenças
    // de fábrica não cabem nem em 1 Hz (RF01.2), e é justamente por isso que
    // a porta está congestionada agora. Cada desligamento alivia a linha em
    // que o próximo comando vai trafegar.
    struct { std::uint8_t id; std::uint8_t taxa; const char* nome; } const
    mensagens[] = {
        {ubx::kNmeaGga, 0, "CFG-MSG GGA off"},
        {ubx::kNmeaGll, 0, "CFG-MSG GLL off"},
        {ubx::kNmeaGsa, 0, "CFG-MSG GSA off"},
        {ubx::kNmeaGsv, 0, "CFG-MSG GSV off"},
        {ubx::kNmeaVtg, 0, "CFG-MSG VTG off"},
        {ubx::kNmeaTxt, 0, "CFG-MSG TXT off"},
        {ubx::kNmeaRmc, 1, "CFG-MSG RMC on"},
    };
    for (const auto& m : mensagens) {
        n = ubx::monta_cfg_msg(ubx::kClasseNmea, m.id, m.taxa, q, sizeof q);
        switch (comanda(q, n, ubx::kCfgMsg, m.nome, log)) {
            case Passo::Ok:       break;
            case Passo::Nak:      return ResultadoConfigGps::Recusado;
            case Passo::Silencio: return ResultadoConfigGps::SemResposta;
        }
    }

    // --- 3. baud, e aqui está a armadilha ---------------------------------
    //
    // A resposta ao `CFG-PRT` sai na velocidade NOVA: o módulo troca ao
    // processar o comando. Se a nossa porta continuasse em 9600 esperaríamos
    // um ACK ilegível e concluiríamos "não respondeu" — com o módulo tendo
    // respondido.
    //
    // E a u-blox **não garante** esse ACK. Por isso `tolera_silencio`: o
    // silêncio aqui não é falha, e quem julga é o passo seguinte, que só
    // funciona se a porta nova estiver de pé.
    n = ubx::monta_cfg_prt_uart(kBaudDesejado, q, sizeof q);
    uart_.escreve(q, n);
    uart_.define_baud(kBaudDesejado);
    baud_final_ = kBaudDesejado;
    log.info("gps", "CFG-PRT enviado; porta local trocada para 115200");
    bool recusou = false;
    if (espera_ack(ubx::kCfgPrt, log, &recusou)) {
        log.info("gps", "CFG-PRT: ACK no baud novo");
    } else if (recusou) {
        log.error("gps", "CFG-PRT: NAK, o modulo recusou o baud");
        return ResultadoConfigGps::Recusado;
    } else {
        log.info("gps", "CFG-PRT sem ACK; a u-blox nao o garante. O CFG-CFG "
                        "adiante e quem confirma que a porta nova responde");
    }

    // --- 4. persistir ------------------------------------------------------
    //
    // Este passo faz dois trabalhos. Grava a configuração, e **prova que o
    // baud novo funciona**: se a porta tivesse ficado muda, nenhum ACK
    // chegaria aqui.
    n = ubx::monta_cfg_cfg(0xFFFFU, ubx::kDispBbr | ubx::kDispEeprom, q,
                           sizeof q);
    if (comanda(q, n, ubx::kCfgCfg, "CFG-CFG persistir", log) != Passo::Ok) {
        return ResultadoConfigGps::NaoPersistiu;
    }

    log.info("gps", "==== receptor configurado: 4 Hz, so RMC, 115200 ====");
    return ResultadoConfigGps::Configurado;
}

}  // namespace coruja
