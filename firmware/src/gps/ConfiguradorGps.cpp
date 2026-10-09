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
                                                const char* nome, Logger& log,
                                                bool obrigatorio) {
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
            if (obrigatorio) { log.error("gps", msg); }
            else             { log.warning("gps", msg); }
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

    // --- 1. achar o módulo, e só então pedir a taxa -----------------------
    //
    // O baud de fábrica é 9600, mas **assumi-lo é o que cegou o aparelho em
    // 08/10/2026**. O passo 4 adiante persiste a configuração em BBR, e a
    // bateria de backup a mantém por horas: numa parada curta o módulo volta
    // falando 115200 enquanto o Pico religa em 9600. Nada do que ele diz é
    // reconhecível, nenhum ACK chega, e o configurador desistia — com o
    // receptor do outro lado funcionando perfeitamente, no outro baud.
    //
    // Sondar custa até 2 s, e só para quem não respondeu a 9600. O módulo
    // recém-saído da caixa responde na primeira e não paga nada.
    std::size_t n = ubx::monta_cfg_rate(kPeriodoNavegacaoMs, q, sizeof q);
    constexpr std::uint32_t kBaudsASondar[] = {kBaudFabrica, kBaudDesejado};
    bool achou = false;
    for (const std::uint32_t baud : kBaudsASondar) {
        if (baud != baud_final_) {
            char msg[128];
            std::snprintf(msg, sizeof msg,
                          "%lu nao respondeu; sondando %lu (o modulo pode ter "
                          "lembrado a configuracao)",
                          static_cast<unsigned long>(baud_final_),
                          static_cast<unsigned long>(baud));
            log.warning("gps", msg);
            uart_.define_baud(baud);
            baud_final_ = baud;
        }
        const Passo r = comanda(q, n, ubx::kCfgRate, "CFG-RATE 250 ms", log);
        if (r == Passo::Nak) { return ResultadoConfigGps::Recusado; }
        if (r == Passo::Ok)  { achou = true; break; }
    }
    if (!achou) {
        // Deixar a porta em 115200 aqui condenaria quem lê depois a um fluxo
        // ilegível mesmo que o módulo estivesse a 9600 o tempo todo.
        uart_.define_baud(kBaudFabrica);
        baud_final_ = kBaudFabrica;
        return ResultadoConfigGps::SemResposta;
    }

    // --- 2. só a RMC na porta ---------------------------------------------
    //
    // Desligar ANTES de subir o baud é de propósito: a 9600 as sete sentenças
    // de fábrica não cabem nem em 1 Hz (RF01.2), e é justamente por isso que
    // a porta está congestionada agora. Cada desligamento alivia a linha em
    // que o próximo comando vai trafegar.
    //
    // **Nem toda sentença é obrigatória.** A TXT carrega avisos de
    // diagnóstico que o parser já descarta, e este NEO-M8N recusa desligá-la:
    // nove dos catorze boots do log de 08/10/2026 terminam em `TXT off: NAK`.
    // Abortar ali deixava a RMC sem ser ligada e a porta em 9600 — o aparelho
    // trabalhava com a configuração pela metade sem dizer uma palavra.
    struct { std::uint8_t id; std::uint8_t taxa; const char* nome;
             bool obrigatorio; } const
    mensagens[] = {
        {ubx::kNmeaGga, 0, "CFG-MSG GGA off", true},
        {ubx::kNmeaGll, 0, "CFG-MSG GLL off", true},
        {ubx::kNmeaGsa, 0, "CFG-MSG GSA off", true},
        {ubx::kNmeaGsv, 0, "CFG-MSG GSV off", true},
        {ubx::kNmeaVtg, 0, "CFG-MSG VTG off", true},
        {ubx::kNmeaTxt, 0, "CFG-MSG TXT off", false},
        {ubx::kNmeaRmc, 1, "CFG-MSG RMC on",  true},
    };
    for (const auto& m : mensagens) {
        n = ubx::monta_cfg_msg(ubx::kClasseNmea, m.id, m.taxa, q, sizeof q);
        const Passo r = comanda(q, n, ubx::kCfgMsg, m.nome, log,
                                m.obrigatorio);
        if (r == Passo::Ok) { continue; }
        if (!m.obrigatorio) {
            log.warning("gps", "o passo acima e opcional; seguindo");
            continue;
        }
        return r == Passo::Nak ? ResultadoConfigGps::Recusado
                               : ResultadoConfigGps::SemResposta;
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
