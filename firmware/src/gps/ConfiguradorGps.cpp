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

bool ConfiguradorGps::espera_ack(std::uint8_t id, Logger& log, bool* recusou,
                                 unsigned esperas) {
    *recusou = false;
    std::uint8_t bloco[128];
    for (unsigned volta = 0; volta < esperas; ++volta) {
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

ConfiguradorGps::Passo ConfiguradorGps::sonda(const std::uint8_t* quadro,
                                              std::size_t tamanho,
                                              std::uint8_t id, Logger& log) {
    uart_.escreve(quadro, tamanho);
    bool recusou = false;
    if (espera_ack(id, log, &recusou, kEsperasDaSondagem)) {
        return Passo::Ok;
    }
    return recusou ? Passo::Nak : Passo::Silencio;
}

bool ConfiguradorGps::acha_o_modulo(const std::uint8_t* quadro,
                                    std::size_t tamanho, Logger& log,
                                    bool* recusou) {
    *recusou = false;
    // A ordem comeca pelo baud em que a porta JA esta. Na partida e o de
    // fabrica; na reconfiguracao em voo (RF01.5) e o que ja funcionava, e
    // repeti-lo poupa uma troca de velocidade por nada.
    const std::uint32_t outro =
        baud_final_ == kBaudFabrica ? kBaudDesejado : kBaudFabrica;
    const std::uint32_t candidatos[] = {baud_final_, outro};

    // --- primeiro, duas perguntas curtas ----------------------------------
    //
    // ⚠️ **Nao se insiste no baud errado.** Uma segunda tentativa so
    // recupera quadro perdido, e no baud errado nao ha quadro perdido: o
    // modulo esta falando outra velocidade e nao vai responder por mais que
    // se espere. Medido em 15 e 16/10/2026, insistir custava 1,74 s em toda
    // partida morna -- com o receptor fixando em 0,5 s do outro lado.
    for (const std::uint32_t baud : candidatos) {
        if (baud != baud_final_) {
            char msg[128];
            std::snprintf(msg, sizeof msg,
                          "%lu nao respondeu de imediato; sondando %lu (o "
                          "modulo pode ter lembrado a configuracao)",
                          static_cast<unsigned long>(baud_final_),
                          static_cast<unsigned long>(baud));
            log.info("gps", msg);
            uart_.define_baud(baud);
            baud_final_ = baud;
        }
        const Passo r = sonda(quadro, tamanho, ubx::kCfgRate, log);
        if (r == Passo::Nak) {
            // Entendeu e recusou: esta no baud certo, e procurar no outro
            // seria procurar no lugar errado.
            *recusou = true;
            return false;
        }
        if (r == Passo::Ok) { return true; }
    }

    // --- ninguem respondeu depressa: a pergunta mudou ---------------------
    //
    // Deixou de ser "qual velocidade" e passou a ser "ha alguem ai". Agora
    // vale gastar a janela inteira antes de dizer que o receptor sumiu --
    // foi abortar cedo demais que deixou o aparelho mudo em 08/10/2026.
    log.warning("gps", "nenhum baud respondeu depressa; insistindo nos dois");
    for (const std::uint32_t baud : candidatos) {
        if (baud != baud_final_) {
            uart_.define_baud(baud);
            baud_final_ = baud;
        }
        const Passo r = comanda(quadro, tamanho, ubx::kCfgRate,
                                "CFG-RATE 250 ms", log);
        if (r == Passo::Nak) { *recusou = true; return false; }
        if (r == Passo::Ok)  { return true; }
    }
    return false;
}

ResultadoConfigGps ConfiguradorGps::executa_com_tentativas(
        Logger& log, unsigned tentativas) {
    ResultadoConfigGps r = ResultadoConfigGps::SemResposta;
    for (unsigned i = 1; i <= tentativas; ++i) {
        r = executa(log);
        if (r == ResultadoConfigGps::Configurado ||
            r == ResultadoConfigGps::Recusado) {
            return r;
        }
        if (i < tentativas) {
            char msg[128];
            std::snprintf(msg, sizeof msg,
                          "configuracao incompleta (%s); tentando de novo "
                          "(%u de %u)", descreve(r), i + 1, tentativas);
            log.warning("gps", msg);
        }
    }
    return r;
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
    // ⚠️ **A sonda pede 1 Hz, não 4.** Ver `kPeriodoFabricaMs`: a esta
    // altura o módulo ainda pode estar a 9600 com as sete sentenças de
    // fábrica, e subir a taxa aqui satura a linha em que os próximos ACKs
    // precisam trafegar. A taxa de verdade é pedida no passo 4, já a 115200
    // e já com só duas sentenças ligadas.
    std::size_t n = ubx::monta_cfg_rate(
        static_cast<std::uint16_t>(kPeriodoFabricaMs), q, sizeof q);
    bool recusou_na_busca = false;
    if (!acha_o_modulo(q, n, log, &recusou_na_busca)) {
        if (recusou_na_busca) { return ResultadoConfigGps::Recusado; }
        // Deixar a porta em 115200 aqui condenaria quem lê depois a um fluxo
        // ilegível mesmo que o módulo estivesse a 9600 o tempo todo.
        uart_.define_baud(kBaudFabrica);
        baud_final_ = kBaudFabrica;
        return ResultadoConfigGps::SemResposta;
    }
    log.info("gps", "CFG-RATE 1 Hz: ACK — o modulo esta nesta porta");

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
        // **Ligada**, e esta é a única que traz o HDOP. A RMC diz onde o
        // receptor acha que está; a GGA diz o quanto isso vale — satélites
        // usados e a geometria deles. Sem ela o aparelho não distingue um
        // fix de 3 m de um de 60 m, e trata os dois como verdade.
        //
        // Cabe no enlace: ~80 B a 4 Hz são 320 B/s, contra 11.520 B/s a
        // 115200. A GSV é que seria cara, e segue desligada.
        {ubx::kNmeaGga, 1, "CFG-MSG GGA on", true},
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

    // --- 3b. a taxa de verdade, agora que a linha aguenta -----------------
    //
    // 🔴 **Este passo estava no começo da sequência até 2026-10-26.** Pedir
    // 4 Hz com o módulo ainda a 9600 e com as sete sentenças de fábrica
    // ligadas pede ~1 960 B/s de um enlace que entrega 960: a linha satura e
    // o ACK do comando seguinte se perde. O diário de 24/10 tem o desfecho —
    // a sequência abortou no `CFG-MSG GGA`, a porta ficou em 9600, e o
    // aparelho rodou 487 s a 1,3 Hz sem que nada o corrigisse.
    //
    // Aqui a conta é outra: RMC + GGA a 4 Hz são ~640 B/s contra 11 520 B/s.
    n = ubx::monta_cfg_rate(kPeriodoNavegacaoMs, q, sizeof q);
    if (comanda(q, n, ubx::kCfgRate, "CFG-RATE 250 ms", log) != Passo::Ok) {
        return ResultadoConfigGps::SemResposta;
    }

    // --- 3c. modelo dinâmico ----------------------------------------------
    //
    // `Automotive`. O padrão de fábrica é `Portable`, que supõe menos
    // aceleração do que um carro faz — e o filtro de navegação resiste a
    // aceitar a mudança de velocidade, com atraso justamente em arrancada e
    // frenagem.
    //
    // **Opcional**, como a TXT e o AOP: um módulo que o recuse não pode
    // custar a RMC e o baud.
    n = ubx::monta_cfg_nav5_automotivo(q, sizeof q);
    if (comanda(q, n, ubx::kCfgNav5, "CFG-NAV5 automotivo", log,
                /*obrigatorio=*/false) != Passo::Ok) {
        log.warning("gps", "sem modelo automotivo; a velocidade na tela vai "
                           "demorar mais a acompanhar o carro");
    }

    // --- 3d. previsão autônoma de órbita -----------------------------------
    //
    // Existe por causa de 14/10/2026: o primeiro fix levou **2 min 48 s** com
    // o carro parado um minuto numa garagem descoberta — seis vezes o cold
    // start de 26 s do datasheet. O AOP faz o módulo prever a órbita a partir
    // das efemérides que já capturou, em vez de esperar a transmissão.
    //
    // **Opcional de propósito**, como a TXT: é um ganho de partida, e um
    // módulo que o recuse não pode custar a RMC e o baud — foi abortar num
    // NAK esperado que deixou o aparelho mudo em 08/10.
    //
    // ⚠️ Vem ANTES do `CFG-CFG`: depois, a previsão não entraria na
    // configuração gravada e se perderia no próximo boot, que é justamente
    // quando ela serve.
    n = ubx::monta_cfg_navx5_aop(true, q, sizeof q);
    if (comanda(q, n, ubx::kCfgNavx5, "CFG-NAVX5 AOP on", log,
                /*obrigatorio=*/false) != Passo::Ok) {
        log.warning("gps", "sem previsao de orbita (AOP); o primeiro fix de "
                           "cada ligacao vai demorar mais");
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

    log.info("gps", "==== receptor configurado: 4 Hz, RMC + GGA, 115200 ====");
    return ResultadoConfigGps::Configurado;
}

}  // namespace coruja
