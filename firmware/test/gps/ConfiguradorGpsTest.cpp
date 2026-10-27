#include "gps/ConfiguradorGps.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "apoio/LoggerMock.h"

namespace {

using namespace coruja;

/// Simula o NEO-M8N do outro lado da porta: recebe quadros UBX, reconhece
/// classe e id, e responde `ACK` ou `NAK` conforme o roteiro.
///
/// É **simulador** e não dublê passivo: implementa o lado do módulo no
/// protocolo — responde pelo comando errado, cala só em certos comandos,
/// exige o baud novo, intercala NMEA. É esse comportamento que permite
/// exercitar a conversa inteira sem o receptor em mãos.
class SimuladorUart : public Uart {
public:
    // --- roteiro
    /// Ids de CFG que devem receber NAK em vez de ACK.
    std::vector<std::uint8_t> recusa;
    /// Ids NMEA cujo `CFG-MSG` deve receber NAK. Granularidade de sentença:
    /// o módulo de verdade recusa a TXT e aceita as outras seis, e `recusa`
    /// (por id de CFG) não sabe distinguir uma da outra.
    std::vector<std::uint8_t> recusa_nmea;
    /// Ids de CFG a que o módulo simplesmente não responde.
    std::vector<std::uint8_t> muda;
    /// Quantas respostas engolir antes de começar a responder (testa
    /// retentativa).
    unsigned engole = 0;
    /// Quantos quadros engolir **logo depois de cada troca de baud**.
    ///
    /// Modela o que o diário de 26/10/2026 mostra em 13 de 15 partidas: com
    /// o módulo a 115200, a sonda curta naquele baud fica sem resposta por
    /// 221 ms, e dois segundos depois o MESMO quadro no MESMO baud é
    /// respondido em 29 ms. O quadro anterior, mandado a 9600 para um módulo
    /// que escuta a 115200, chega como ruído — e ruído pode abrir um quadro
    /// UBX fantasma que engole o quadro bom seguinte.
    ///
    /// Diferente de `engole`, que conta desde o primeiro comando: este conta
    /// a partir da troca, e por isso não muda de valor conforme o
    /// configurador sonde uma ou duas vezes por baud.
    ///
    /// É um ORÇAMENTO para a execução inteira, não por troca: o `CFG-PRT`
    /// também troca o baud, e rearmar ali faria o teste medir dois fenômenos
    /// de uma vez — a sondagem e a retentativa do comando seguinte.
    unsigned engole_apos_trocar_baud = 0;
    /// O módulo só responde quando a porta está neste baud. Zero = qualquer.
    std::uint32_t baud_exigido = 0;
    /// Texto NMEA injetado junto das respostas, como na linha de verdade.
    bool intercala_nmea = false;
    /// Responde ACK, mas dizendo confirmar OUTRO comando. Acontece de
    /// verdade: sobra de ACK de um comando anterior na fila.
    bool responde_pelo_comando_errado = false;

    // --- observação
    struct Comando { std::uint8_t classe, id; std::vector<std::uint8_t> payload; };
    std::vector<Comando>       comandos;
    std::vector<std::uint32_t> bauds;          ///< sequência de define_baud
    /// Baud vigente quando cada comando foi escrito. `bauds` sozinho não
    /// serve para afirmar ORDEM de sondagem: ele já começa com o de fábrica
    /// porque é onde a porta nasce, e um configurador que sondasse na ordem
    /// trocada teria o mesmo `bauds.front()`.
    std::vector<std::uint32_t> baud_do_comando;
    /// Em que índice de `comandos` o baud mudou. Revela a ORDEM.
    int comandos_ate_trocar_baud = -1;

    SimuladorUart() { bauds.push_back(kBaudFabrica); }

    void escreve(const std::uint8_t* bytes, std::size_t tamanho) override {
        // Só entende quadro UBX inteiro, que é o que o configurador manda.
        if (tamanho < 8 || bytes[0] != ubx::kSync1 || bytes[1] != ubx::kSync2) {
            return;
        }
        Comando c{bytes[2], bytes[3], {}};
        const std::size_t tam = bytes[4] | (bytes[5] << 8);
        c.payload.assign(bytes + 6, bytes + 6 + tam);
        comandos.push_back(c);
        baud_do_comando.push_back(bauds.back());
        responde(c);
    }

    std::size_t le(std::uint8_t* destino, std::size_t capacidade) override {
        const std::size_t n = pendente_.size() < capacidade ? pendente_.size()
                                                            : capacidade;
        for (std::size_t i = 0; i < n; ++i) { destino[i] = pendente_[i]; }
        pendente_.erase(pendente_.begin(),
                        pendente_.begin() + static_cast<long>(n));
        return n;
    }

    void define_baud(std::uint32_t baud) override {
        bauds.push_back(baud);
        comandos_ate_trocar_baud = static_cast<int>(comandos.size());
        logo_apos_troca_ = true;
    }

    bool mandou(std::uint8_t id) const {
        for (const auto& c : comandos) {
            if (c.classe == ubx::kClasseCfg && c.id == id) { return true; }
        }
        return false;
    }
    /// O primeiro comando com este id, para olhar o payload. Aborta no
    /// teste se nao houver -- `mandou()` responde essa pergunta.
    const Comando& comando(std::uint8_t id) const {
        for (const auto& c : comandos) {
            if (c.classe == ubx::kClasseCfg && c.id == id) { return c; }
        }
        static const Comando vazio{};
        return vazio;
    }
    unsigned quantos(std::uint8_t id) const {
        unsigned n = 0;
        for (const auto& c : comandos) {
            if (c.classe == ubx::kClasseCfg && c.id == id) { ++n; }
        }
        return n;
    }

private:
    void responde(const Comando& c) {
        if (engole > 0) { --engole; return; }
        if (baud_exigido != 0 && bauds.back() != baud_exigido) { return; }
        if (logo_apos_troca_) {
            logo_apos_troca_ = false;
            if (engole_apos_trocar_baud > 0) {
                --engole_apos_trocar_baud;
                return;
            }
        }
        for (const auto id : muda) { if (id == c.id) { return; } }
        bool nak = false;
        for (const auto id : recusa) { if (id == c.id) { nak = true; } }
        if (c.id == ubx::kCfgMsg && c.payload.size() >= 2) {
            for (const auto id : recusa_nmea) {
                if (id == c.payload[1]) { nak = true; }
            }
        }

        if (intercala_nmea) {
            const char* s = "$GNRMC,123519.00,V,,,,,,,220926,,,N*63\r\n";
            for (const char* p = s; *p != 0; ++p) {
                pendente_.push_back(static_cast<std::uint8_t>(*p));
            }
        }
        const std::uint8_t id_confirmado =
            responde_pelo_comando_errado
                ? static_cast<std::uint8_t>(c.id ^ 0x7FU) : c.id;
        // O modulo de verdade troca a PROPRIA velocidade ao processar o
        // CFG-PRT. Sem isto o simulador nao consegue representar o caso em
        // que ele comeca a 9600 -- BBR drenada -- e termina a 115200, que e
        // exatamente a partida fria de 15/10/2026.
        if (c.classe == ubx::kClasseCfg && c.id == ubx::kCfgPrt &&
            baud_exigido != 0) {
            baud_exigido = kBaudDesejado;
        }
        const std::uint8_t carga[2] = {c.classe, id_confirmado};
        std::uint8_t quadro[16];
        const std::size_t n = ubx::monta(ubx::kClasseAck,
                                         nak ? ubx::kAckNak : ubx::kAckAck,
                                         carga, sizeof carga, quadro,
                                         sizeof quadro);
        for (std::size_t i = 0; i < n; ++i) { pendente_.push_back(quadro[i]); }
    }

    std::vector<std::uint8_t> pendente_;
    bool logo_apos_troca_ = false;
};

class PausaFalsa : public Pausa {
public:
    unsigned esperas = 0;
    std::uint32_t agora = 0;
    void espera_ms(std::uint32_t ms) override { ++esperas; agora += ms; }
    std::uint32_t agora_ms() override { return agora; }
};

struct Bancada {
    SimuladorUart         uart;
    PausaFalsa        pausa;
    teste::LoggerMock log;
    ResultadoConfigGps roda() {
        ConfiguradorGps c(uart, pausa);
        const auto r = c.executa(log);
        baud_final = c.baud_final();
        return r;
    }
    std::uint32_t baud_final = 0;
};

// ================================================ a sondagem curta (16/10)
//
// ⚠️ **Quatro partidas de verdade mataram a ideia anterior.** O aparelho
// guardava em que baud o modulo tinha respondido e comecava por ele. Medido
// em 15 e 16/10/2026, o padrao real foi outro:
//
//     1a partida do dia    9600     (a BBR do receptor drenou de um dia
//     logo em seguida      115200    para o outro; numa parada curta ela
//     1a partida do dia    9600      sobrevive e ele lembra os 115200)
//     logo em seguida      115200
//
// Nao e "igual a ultima vez" -- e "9600 depois de muitas horas desligado".
// A lembranca previa o CONTRARIO em toda transicao, e nessa amostra toda
// partida e uma transicao: ela erraria 4 de 4, enquanto comecar sempre em
// 9600 acerta 2 de 4. Era pior que nao ter nada.
//
// O conserto nao e adivinhar melhor, e **perguntar barato**: uma tentativa
// curta por baud. Insistir no baud errado nao recupera nada -- o modulo esta
// fisicamente falando outra velocidade --, e so depois de saber qual e que a
// repeticao passa a fazer sentido.

TEST(ConfiguradorGps, a_sondagem_certa_de_primeira_quase_nao_custa) {
    // DOIS CFG-RATE agora, e de proposito: o primeiro e a sonda, a 1 Hz; o
    // segundo pede os 4 Hz depois que o baud ja subiu. Ver
    // `o_4_hz_so_e_pedido_depois_do_baud`.
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    EXPECT_EQ(b.uart.quantos(ubx::kCfgRate), 2u)
        << "sondou mais de uma vez com o modulo respondendo";
}

TEST(ConfiguradorGps, no_baud_errado_desiste_depressa_e_acha_no_outro) {
    // O caso de toda partida morna: a BBR sobreviveu e o modulo fala 115200
    // enquanto a porta abre em 9600.
    Bancada b;
    b.uart.baud_exigido = kBaudDesejado;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    EXPECT_EQ(b.baud_final, kBaudDesejado);
    // Duas perguntas no baud de fabrica (`kSondagensPorBaud`), uma no outro
    // -- que responde de primeira --, mais o CFG-RATE final dos 4 Hz.
    EXPECT_EQ(b.uart.quantos(ubx::kCfgRate), kSondagensPorBaud + 2U);
    // O tempo nao muda: so o baud ERRADO gasta janela, e a repeticao nele
    // nao custa mais que a janela que ja se gastava la.
    EXPECT_LE(b.pausa.agora,
              kSondagensPorBaud * kEsperasDaSondagem * kEsperaEntreLeiturasMs)
        << "a sondagem gastou mais que as janelas curtas do baud errado";
}

TEST(ConfiguradorGps, a_janela_da_sondagem_e_bem_menor_que_a_do_comando) {
    // ⚠️ O numero que importa: a 9600 um ACK de CFG-RATE sao ~10 bytes, uns
    // 10 ms de linha. A janela do comando normal -- 50 x 20 ms -- existe
    // para quadro perdido, que no baud errado nao e o que esta havendo.
    EXPECT_LT(kEsperasDaSondagem, kEsperasPorAck / 3);
    EXPECT_GE(kEsperasDaSondagem * kEsperaEntreLeiturasMs, 200u)
        << "folga pequena demais sobre o tempo de linha de um ACK";
}

TEST(ConfiguradorGps, modulo_mudo_ainda_insiste_antes_de_desistir) {
    // ⚠️ **A sondagem curta nao pode virar desistencia apressada.** Se
    // nenhum dos dois bauds respondeu depressa, o problema deixou de ser
    // "qual velocidade" e passou a ser "ha alguem ai" -- e aí vale gastar a
    // janela inteira antes de dizer que o receptor sumiu.
    Bancada b;
    b.uart.muda.push_back(ubx::kCfgRate);
    EXPECT_EQ(b.roda(), ResultadoConfigGps::SemResposta);
    EXPECT_GT(b.uart.quantos(ubx::kCfgRate), 2u)
        << "desistiu so com as duas perguntas curtas";
}

TEST(ConfiguradorGps, porta_volta_ao_baud_de_fabrica_quando_ninguem_responde) {
    Bancada b;
    b.uart.muda.push_back(ubx::kCfgRate);
    ASSERT_EQ(b.roda(), ResultadoConfigGps::SemResposta);
    EXPECT_EQ(b.baud_final, kBaudFabrica);
}

TEST(ConfiguradorGps, nak_na_sondagem_recusa_sem_tentar_o_outro_baud) {
    // NAK quer dizer que o modulo ENTENDEU e nao aceitou: ele esta no baud
    // certo, e sondar o outro procuraria no lugar errado.
    Bancada b;
    b.uart.recusa.push_back(ubx::kCfgRate);
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Recusado);
    EXPECT_EQ(b.baud_final, kBaudFabrica);
}

// ================================================== sequência do RF01.2

TEST(ConfiguradorGps, modulo_que_aceita_tudo_fica_configurado) {
    Bancada b;
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    EXPECT_EQ(b.baud_final, kBaudDesejado);
}

TEST(ConfiguradorGps, manda_os_quatro_comandos_do_requisito) {
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    EXPECT_TRUE(b.uart.mandou(ubx::kCfgRate));
    EXPECT_TRUE(b.uart.mandou(ubx::kCfgPrt));
    EXPECT_TRUE(b.uart.mandou(ubx::kCfgMsg));
    EXPECT_TRUE(b.uart.mandou(ubx::kCfgCfg));
}

// ====================================== previsao de orbita (AOP), 14/10/2026

TEST(ConfiguradorGps, liga_a_previsao_de_orbita) {
    // Existe por causa de um primeiro fix de 2 min 48 s -- seis vezes o cold
    // start do datasheet. O AOP faz o modulo prever a orbita dos satelites a
    // partir do que ja capturou, em vez de esperar a transmissao.
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    ASSERT_TRUE(b.uart.mandou(ubx::kCfgNavx5));

    const auto& c = b.uart.comando(ubx::kCfgNavx5);
    ASSERT_EQ(c.payload.size(), 40U);
    EXPECT_EQ(c.payload[27] & 0x01, 0x01) << "o bit useAOP nao foi ligado";
    // ⚠️ mask1 com SO o bit 14: sem isso, os 38 bytes zerados do payload
    // reescreveriam minimo de satelites, C/N0 e rollover de semana.
    EXPECT_EQ(c.payload[2] | (c.payload[3] << 8), 0x4000);
}

TEST(ConfiguradorGps, o_AOP_e_OPCIONAL_e_um_NAK_nele_nao_derruba_o_resto) {
    // Mesma licao da TXT: nove dos catorze boots de 08/10/2026 terminaram em
    // `TXT off: NAK`, e abortar ali deixava a porta em 9600 com a RMC
    // desligada. Um extra recusado nao pode custar o essencial.
    Bancada b;
    b.uart.recusa.push_back(ubx::kCfgNavx5);
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    EXPECT_EQ(b.baud_final, kBaudDesejado);
    EXPECT_TRUE(b.log.contem(Nivel::Warning, "AOP"))
        << "a recusa tem de aparecer no log, ou ninguem descobre";
}

TEST(ConfiguradorGps, o_AOP_e_pedido_ANTES_de_persistir) {
    // Se fosse depois do CFG-CFG, a previsao nao estaria na configuracao
    // gravada -- e seria perdida no proximo boot, que e exatamente quando
    // ela serve.
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    int ordem_aop = -1, ordem_cfg = -1;
    for (std::size_t i = 0; i < b.uart.comandos.size(); ++i) {
        if (b.uart.comandos[i].id == ubx::kCfgNavx5) { ordem_aop = int(i); }
        if (b.uart.comandos[i].id == ubx::kCfgCfg) { ordem_cfg = int(i); }
    }
    ASSERT_GE(ordem_aop, 0);
    ASSERT_GE(ordem_cfg, 0);
    EXPECT_LT(ordem_aop, ordem_cfg);
}

TEST(ConfiguradorGps, pede_250_ms_que_sao_os_4_hz_do_rf01_4) {
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    // O ULTIMO CFG-RATE e o que vale: o primeiro e a sonda, a 1 Hz.
    unsigned ultimo = 0;
    bool achou = false;
    for (const auto& c : b.uart.comandos) {
        if (c.id != ubx::kCfgRate) { continue; }
        ASSERT_GE(c.payload.size(), 2U);
        ultimo = c.payload[0] | (c.payload[1] << 8);
        achou = true;
    }
    ASSERT_TRUE(achou) << "nao mandou CFG-RATE";
    EXPECT_EQ(ultimo, 250U);
}

TEST(ConfiguradorGps, liga_a_RMC_e_a_GGA_e_desliga_as_outras_cinco) {
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    unsigned ligadas = 0, desligadas = 0;
    bool rmc = false, gga = false;
    for (const auto& c : b.uart.comandos) {
        if (c.id != ubx::kCfgMsg) { continue; }
        ASSERT_EQ(c.payload.size(), 3U);
        EXPECT_EQ(c.payload[0], ubx::kClasseNmea);
        if (c.payload[2] == 0) { ++desligadas; }
        else {
            ++ligadas;
            if (c.payload[1] == ubx::kNmeaRmc) { rmc = true; }
            if (c.payload[1] == ubx::kNmeaGga) { gga = true; }
        }
    }
    EXPECT_EQ(ligadas, 2U);
    EXPECT_TRUE(rmc);
    EXPECT_TRUE(gga) << "a GGA traz satelites e HDOP, que a RMC nao tem";
    EXPECT_EQ(desligadas, 5U) << "a GSV sozinha ja estoura a linha a 9600";
}

TEST(ConfiguradorGps, a_GGA_cabe_no_enlace_a_115200) {
    // Conta, nao impressao: a GGA tem ~80 B e sai 4 vezes por segundo, logo
    // ~320 B/s. Somada a RMC (~70 B x 4 = 280 B/s), da ~600 B/s num enlace
    // de 11.520 B/s -- cerca de 5% de ocupacao. A 9600 seriam 62%, e e por
    // isso que o passo do baud vem ANTES de a GGA comecar a sair.
    constexpr unsigned kBytesGga = 80, kBytesRmc = 70, kHz = 4;
    constexpr unsigned kCapacidade115200 = 115200 / 10;   // 8N1
    const unsigned gasto = (kBytesGga + kBytesRmc) * kHz;
    EXPECT_LT(gasto * 100 / kCapacidade115200, 10U);
}

TEST(ConfiguradorGps, pede_115200) {
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    for (const auto& c : b.uart.comandos) {
        if (c.id != ubx::kCfgPrt) { continue; }
        ASSERT_GE(c.payload.size(), 12U);
        const std::uint32_t baud = c.payload[8] | (c.payload[9] << 8) |
                                   (c.payload[10] << 16) | (c.payload[11] << 24);
        EXPECT_EQ(baud, kBaudDesejado);
        return;
    }
    FAIL() << "nao mandou CFG-PRT";
}

// =========================================== a armadilha da troca de baud

TEST(ConfiguradorGps, troca_o_baud_local_LOGO_APOS_mandar_o_CFG_PRT) {
    // A resposta ao CFG-PRT sai na velocidade NOVA. Se a porta local
    // continuasse em 9600, esperariamos um ACK ilegivel e concluiriamos "nao
    // respondeu" -- com o modulo tendo respondido.
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    ASSERT_GE(b.uart.comandos_ate_trocar_baud, 0);
    const auto& ultimo =
        b.uart.comandos[static_cast<std::size_t>(b.uart.comandos_ate_trocar_baud) - 1];
    EXPECT_EQ(ultimo.id, ubx::kCfgPrt)
        << "trocou o baud em outro ponto da sequencia";
}

TEST(ConfiguradorGps, o_modulo_que_so_responde_em_115200_ainda_e_configurado) {
    // ESTE e o defeito de 08/10/2026, e ele nao e hipotetico: o log do
    // aparelho tem dois boots seguidos que morreram aqui, logo depois de um
    // boot que terminou com `CFG-CFG persistir: ACK`.
    //
    // O modulo guarda a configuracao em BBR, que a bateria de backup mantem
    // por horas. Parada curta -- a escola, o posto -- e ele volta falando
    // 115200. O Pico religa assumindo o baud de fabrica, nao reconhece nada,
    // e sem este sondar desistiria com o receptor inteiro do outro lado,
    // funcionando, a 115200. Vinte minutos de viagem sem um fix.
    Bancada b;
    b.uart.baud_exigido = kBaudDesejado;
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    EXPECT_EQ(b.baud_final, kBaudDesejado);
}

TEST(ConfiguradorGps, sonda_o_baud_de_fabrica_ANTES_do_outro) {
    // Ordem importa: o modulo recem-saido da caixa esta a 9600, e esse e o
    // caso que nao pode pagar pedagio nenhum. So quem nao responde a 9600
    // custa a sondagem extra.
    Bancada b;
    b.uart.baud_exigido = kBaudDesejado;    // so responde no outro
    b.roda();
    ASSERT_FALSE(b.uart.baud_do_comando.empty());
    EXPECT_EQ(b.uart.baud_do_comando.front(), kBaudFabrica);
    EXPECT_EQ(b.baud_final, kBaudDesejado);  // e achou no segundo
}

TEST(ConfiguradorGps, modulo_mudo_nos_DOIS_bauds_ainda_e_sem_resposta) {
    // O sondar nao pode transformar "nao ha ninguem ai" em outra coisa:
    // fiacao solta, modulo sem energia e antena arrancada continuam tendo
    // este nome, que e o que manda procurar no lugar certo.
    Bancada b;
    b.uart.muda = {ubx::kCfgRate, ubx::kCfgMsg, ubx::kCfgPrt, ubx::kCfgCfg};
    EXPECT_EQ(b.roda(), ResultadoConfigGps::SemResposta);
}

TEST(ConfiguradorGps, desistir_devolve_a_porta_ao_baud_de_fabrica) {
    // Quem le a porta depois do configurador herda o baud que ele deixou.
    // Desistir no meio da sondagem e deixar a porta em 115200 condenaria o
    // leitor a um fluxo ilegivel mesmo que o modulo estivesse a 9600.
    Bancada b;
    b.uart.muda = {ubx::kCfgRate, ubx::kCfgMsg, ubx::kCfgPrt, ubx::kCfgCfg};
    EXPECT_EQ(b.roda(), ResultadoConfigGps::SemResposta);
    EXPECT_EQ(b.baud_final, kBaudFabrica);
    EXPECT_EQ(b.uart.bauds.back(), kBaudFabrica);
}

TEST(ConfiguradorGps, o_sondar_nao_atropela_o_CFG_RATE_do_modulo_normal) {
    // Modulo de fabrica responde de primeira: nenhuma sondagem EXTRA. Sobram
    // os dois de projeto -- a sonda a 1 Hz e os 4 Hz do fim.
    Bancada b;
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    EXPECT_EQ(b.uart.quantos(ubx::kCfgRate), 2U);
}

// ================== o quadro perdido na troca de baud (27/10/2026)
//
// 🔴 **13 das 15 partidas da v0.2.21 pagaram 2,2 s de sondagem por um quadro
// so.** O diario de 26/10 mostra o padrao identico em todas elas: o modulo
// esta a 115200 -- a configuracao persistiu de verdade --, a sonda curta
// naquele baud fica 221 ms sem resposta, a fase insistente roda os dois bauds
// e o MESMO quadro, no MESMO baud, e respondido em 29 ms.
//
// Que o modulo ja estava acordado as duas partidas restantes provam: nelas a
// BBR tinha drenado, o modulo respondeu a 9600 em 60 ms, e 60 ms e bem antes
// dos 221 que a sonda desperdica. O que falha nao e o tempo -- e o quadro.
//
// O conserto e perguntar duas vezes antes de desistir do baud. Quando o
// modulo responde de primeira nao custa nada (ver
// `o_sondar_nao_atropela_o_CFG_RATE_do_modulo_normal`).

TEST(ConfiguradorGps, a_sonda_insiste_uma_vez_quando_o_quadro_se_perde) {
    Bancada b;
    b.uart.baud_exigido = kBaudDesejado;   // o modulo lembrou os 115200
    b.uart.engole_apos_trocar_baud = 1;    // e o primeiro quadro apos a troca some

    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    EXPECT_FALSE(b.log.contem(Nivel::Warning, "insistindo nos dois"))
        << "caiu na fase longa por um quadro perdido na troca de baud";
}

TEST(ConfiguradorGps, o_quadro_perdido_custa_uma_janela_curta_e_nao_mais) {
    // O preco do conserto, medido como DIFERENCA contra a mesma partida sem
    // perda: exatamente a janela curta da pergunta repetida. Absoluto aqui
    // nao diria nada -- o numero inclui a sequencia inteira.
    Bancada limpa;
    limpa.uart.baud_exigido = kBaudDesejado;
    ASSERT_EQ(limpa.roda(), ResultadoConfigGps::Configurado);

    Bancada b;
    b.uart.baud_exigido = kBaudDesejado;
    b.uart.engole_apos_trocar_baud = 1;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);

    EXPECT_LE(b.pausa.agora - limpa.pausa.agora,
              kEsperasDaSondagem * kEsperaEntreLeiturasMs)
        << "o quadro perdido custou mais que a janela da pergunta repetida";
}

// ============================ a ordem que a viagem de 24/10/2026 cobrou
//
// 🔴 Com o modulo ainda a 9600 e as sete sentencas de fabrica ligadas, pedir
// 4 Hz pede ~1 960 B/s de um enlace que entrega 960. A linha satura, o ACK do
// CFG-MSG seguinte se perde, a sequencia aborta -- e o aparelho roda assim a
// viagem inteira. Foram 487 s a 1,3 Hz naquele sabado.

TEST(ConfiguradorGps, a_sonda_pede_1_hz_e_nao_4) {
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    for (const auto& c : b.uart.comandos) {
        if (c.id != ubx::kCfgRate) { continue; }
        ASSERT_GE(c.payload.size(), 2U);
        const unsigned ms = c.payload[0] | (c.payload[1] << 8);
        EXPECT_EQ(ms, 1000U) << "a sonda subiu a taxa antes de aliviar a linha";
        return;
    }
    FAIL() << "nao mandou CFG-RATE";
}

TEST(ConfiguradorGps, o_4_hz_so_e_pedido_depois_do_baud) {
    // ⚠️ A ordem e o conserto inteiro. Se os 4 Hz sairem antes do CFG-PRT,
    // eles saem no enlace de 9600 -- que e exatamente a saturacao de 24/10.
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    int i_prt = -1, i_4hz = -1;
    for (std::size_t i = 0; i < b.uart.comandos.size(); ++i) {
        const auto& c = b.uart.comandos[i];
        if (c.id == ubx::kCfgPrt) { i_prt = static_cast<int>(i); }
        if (c.id == ubx::kCfgRate && c.payload.size() >= 2 &&
            (c.payload[0] | (c.payload[1] << 8)) == 250) {
            i_4hz = static_cast<int>(i);
        }
    }
    ASSERT_GE(i_prt, 0);
    ASSERT_GE(i_4hz, 0);
    EXPECT_GT(i_4hz, i_prt) << "pediu 4 Hz antes de subir o baud";
}

TEST(ConfiguradorGps, as_sentencas_sao_silenciadas_antes_dos_4_hz) {
    // O outro lado da mesma conta: a 9600 as sete de fabrica nao cabem nem
    // a 1 Hz, quanto mais a 4.
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    int i_gsv = -1, i_4hz = -1;
    for (std::size_t i = 0; i < b.uart.comandos.size(); ++i) {
        const auto& c = b.uart.comandos[i];
        if (c.id == ubx::kCfgMsg && c.payload.size() >= 2 &&
            c.payload[1] == ubx::kNmeaGsv) {
            i_gsv = static_cast<int>(i);
        }
        if (c.id == ubx::kCfgRate && c.payload.size() >= 2 &&
            (c.payload[0] | (c.payload[1] << 8)) == 250) {
            i_4hz = static_cast<int>(i);
        }
    }
    ASSERT_GE(i_gsv, 0) << "nao desligou a GSV";
    ASSERT_GE(i_4hz, 0);
    EXPECT_GT(i_4hz, i_gsv) << "subiu a taxa com a GSV ainda ligada";
}

TEST(ConfiguradorGps, pede_o_modelo_automotivo) {
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    EXPECT_TRUE(b.uart.mandou(ubx::kCfgNav5));
}

TEST(ConfiguradorGps, modelo_automotivo_recusado_nao_derruba_a_sequencia) {
    // Opcional, como a TXT e o AOP: um ganho de resposta nao pode custar a
    // RMC e o baud.
    Bancada b;
    b.uart.recusa.push_back(ubx::kCfgNav5);
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Configurado);
}

// ================================================== passos opcionais

TEST(ConfiguradorGps, NAK_na_TXT_nao_derruba_a_configuracao) {
    // O NEO-M8N deste aparelho recusa desligar a TXT: nove dos catorze boots
    // do log de 08/10/2026 terminam em `TXT off: NAK`. A TXT e ruido de
    // diagnostico que o parser ja descarta -- desistir da configuracao
    // inteira por causa dela e que era desproporcional.
    Bancada b;
    b.uart.recusa_nmea.push_back(ubx::kNmeaTxt);
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    EXPECT_EQ(b.baud_final, kBaudDesejado);
}

TEST(ConfiguradorGps, mesmo_recusando_a_TXT_a_RMC_ainda_e_ligada) {
    // O prejuizo real do NAK da TXT era a sequencia abortar ANTES da RMC.
    Bancada b;
    b.uart.recusa_nmea.push_back(ubx::kNmeaTxt);
    b.roda();
    bool ligou_rmc = false;
    for (const auto& c : b.uart.comandos) {
        if (c.id == ubx::kCfgMsg && c.payload.size() >= 3 &&
            c.payload[1] == ubx::kNmeaRmc && c.payload[2] == 1) {
            ligou_rmc = true;
        }
    }
    EXPECT_TRUE(ligou_rmc);
}

TEST(ConfiguradorGps, NAK_numa_sentenca_OBRIGATORIA_continua_recusando) {
    // A tolerancia e da TXT, nao de qualquer NAK. Um modulo que recusa
    // desligar a GSV deixa a porta congestionada a 9600, que e o sintoma
    // que o RF01.2 existe para evitar.
    Bancada b;
    b.uart.recusa_nmea.push_back(ubx::kNmeaGsv);
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Recusado);
}

TEST(ConfiguradorGps, NAK_na_RMC_continua_recusando) {
    Bancada b;
    b.uart.recusa_nmea.push_back(ubx::kNmeaRmc);
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Recusado);
}

TEST(ConfiguradorGps, silencio_no_CFG_PRT_e_tolerado) {
    // A u-blox nao garante esse ACK. Tratar o silencio como falha recusaria
    // um modulo que se configurou certo.
    Bancada b;
    b.uart.muda.push_back(ubx::kCfgPrt);
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Configurado);
}

// ================================================== falhas e diagnóstico

TEST(ConfiguradorGps, modulo_calado_e_sem_resposta_e_nao_recusa) {
    // Silencio aponta para fiacao, energia ou baud errado; recusa diz que o
    // modulo entendeu e nao aceitou. Sao dois lugares diferentes para
    // procurar.
    Bancada b;
    b.uart.muda = {ubx::kCfgRate, ubx::kCfgMsg, ubx::kCfgPrt, ubx::kCfgCfg};
    EXPECT_EQ(b.roda(), ResultadoConfigGps::SemResposta);
}

TEST(ConfiguradorGps, NAK_no_primeiro_comando_e_recusa) {
    Bancada b;
    b.uart.recusa.push_back(ubx::kCfgRate);
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Recusado);
}

TEST(ConfiguradorGps, NAK_numa_mensagem_e_recusa) {
    Bancada b;
    b.uart.recusa.push_back(ubx::kCfgMsg);
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Recusado);
}

TEST(ConfiguradorGps, NAK_nao_e_repetido) {
    // Recusa nao melhora insistindo: o modulo entendeu e nao aceitou.
    Bancada b;
    b.uart.recusa.push_back(ubx::kCfgRate);
    b.roda();
    EXPECT_EQ(b.uart.quantos(ubx::kCfgRate), 1U);
}

TEST(ConfiguradorGps, o_silencio_total_tem_numero_de_tentativas_FIXO) {
    // `kSondagensPorBaud` perguntas curtas em cada baud, e so entao
    // `kTentativasPorComando` longas em cada um, antes de declarar que nao ha
    // ninguem do outro lado.
    //
    // A garantia original -- nao insistir indefinidamente -- continua
    // valendo. O que mudou em 16/10/2026 e a ORDEM: as duas perguntas
    // baratas vem primeiro, e quem fala em algum dos dois bauds nunca chega
    // as caras.
    Bancada b;
    b.uart.muda = {ubx::kCfgRate, ubx::kCfgMsg, ubx::kCfgPrt, ubx::kCfgCfg};
    b.roda();
    EXPECT_EQ(b.uart.quantos(ubx::kCfgRate),
              kSondagensPorBaud * 2 + kTentativasPorComando * 2);
}

TEST(ConfiguradorGps, recupera_na_segunda_tentativa) {
    Bancada b;
    b.uart.engole = 1;   // a primeira resposta some
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    // A sonda repetida, mais o CFG-RATE dos 4 Hz no fim.
    EXPECT_EQ(b.uart.quantos(ubx::kCfgRate), 3U);
}

TEST(ConfiguradorGps, falha_so_ao_persistir_e_dito_com_esse_nome) {
    // Configurou e nao gravou: funciona ate o proximo boot. E um desfecho
    // diferente de nao ter configurado, e o nome tem de dizer isso.
    Bancada b;
    b.uart.muda.push_back(ubx::kCfgCfg);
    EXPECT_EQ(b.roda(), ResultadoConfigGps::NaoPersistiu);
}

// =============================================== ruído na mesma linha

TEST(ConfiguradorGps, sentencas_NMEA_entre_as_respostas_nao_atrapalham) {
    // E a situacao real: a linha carrega NMEA de texto e UBX binario ao
    // mesmo tempo, e o modulo segue emitindo enquanto se configura.
    Bancada b;
    b.uart.intercala_nmea = true;
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Configurado);
}

TEST(ConfiguradorGps, ACK_de_outro_comando_nao_confirma_este) {
    // Chega ACK de comando anterior na mesma fila. Aceitar qualquer ACK daria
    // por confirmado um comando que o modulo pode nem ter visto -- e o
    // sintoma so apareceria meses depois, como taxa de 1 Hz.
    Bancada b;
    b.uart.responde_pelo_comando_errado = true;
    EXPECT_EQ(b.roda(), ResultadoConfigGps::SemResposta);
}

TEST(ConfiguradorGps, NAK_no_CFG_PRT_e_recusa_e_nao_silencio_tolerado) {
    // O silencio no CFG-PRT e tolerado porque a u-blox nao garante o ACK.
    // Um NAK e outra coisa: o modulo entendeu e recusou o baud, e seguir
    // adiante deixaria a porta num estado que ninguem pediu.
    Bancada b;
    b.uart.recusa.push_back(ubx::kCfgPrt);
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Recusado);
}

TEST(ConfiguradorGps, descricoes_cobrem_todos_os_desfechos) {
    for (const auto r : {ResultadoConfigGps::Configurado,
                         ResultadoConfigGps::SemResposta,
                         ResultadoConfigGps::Recusado,
                         ResultadoConfigGps::NaoPersistiu}) {
        EXPECT_STRNE(descreve(r), "?");
    }
}


// ====================================== repetir quando a sequencia aborta
//
// 🔴 Em 24/10/2026 a sequencia abortou no segundo comando e o aparelho
// seguiu em frente: a viagem inteira correu com o receptor meio
// configurado, 487 s a 1,3 Hz. Desistir na primeira era aceitar isso calado.

TEST(ConfiguradorGps, silencio_na_primeira_tenta_de_novo) {
    Bancada b;
    b.uart.muda.push_back(ubx::kCfgMsg);   // some com os ACK de CFG-MSG
    ConfiguradorGps c{b.uart, b.pausa};
    ASSERT_NE(c.executa_com_tentativas(b.log), ResultadoConfigGps::Configurado);
    EXPECT_GE(b.uart.quantos(ubx::kCfgMsg), 2U)
        << "desistiu na primeira tentativa";
}

TEST(ConfiguradorGps, uma_recusa_NAO_e_repetida) {
    // ⚠️ Recusa e resposta: o modulo entendeu e nao aceitou. Insistir so
    // atrasaria o diagnostico -- mesmo raciocinio do `comanda`.
    Bancada b;
    b.uart.recusa.push_back(ubx::kCfgRate);
    ConfiguradorGps c{b.uart, b.pausa};
    EXPECT_EQ(c.executa_com_tentativas(b.log), ResultadoConfigGps::Recusado);
    EXPECT_LE(b.uart.quantos(ubx::kCfgRate), 1U) << "repetiu um NAK";
}

TEST(ConfiguradorGps, dando_certo_na_primeira_nao_repete_nada) {
    Bancada b;
    ConfiguradorGps c{b.uart, b.pausa};
    ASSERT_EQ(c.executa_com_tentativas(b.log), ResultadoConfigGps::Configurado);
    EXPECT_EQ(b.uart.quantos(ubx::kCfgCfg), 1U);
}

TEST(ConfiguradorGps, a_repeticao_aparece_no_log) {
    // Sem a linha, "por que o boot demorou" vira adivinhacao.
    Bancada b;
    b.uart.muda.push_back(ubx::kCfgMsg);
    ConfiguradorGps c{b.uart, b.pausa};
    c.executa_com_tentativas(b.log);
    bool achou = false;
    for (const auto& e : b.log.entradas()) {
        if (e.mensagem.find("tentando de novo") != std::string::npos) {
            achou = true;
        }
    }
    EXPECT_TRUE(achou);
}


TEST(ConfiguradorGps, a_sonda_se_anuncia_como_1_hz_no_log) {
    // 🔴 **Achado no diario de 26/10/2026.** O rotulo dizia "CFG-RATE 250 ms"
    // enquanto a sonda mandava 1 Hz, e as linhas saiam assim:
    //
    //     CFG-RATE 250 ms: sem resposta (tentativa 1 de 2)
    //     CFG-RATE 250 ms: ACK
    //     CFG-RATE 1 Hz: ACK -- o modulo esta nesta porta
    //
    // Quem lesse concluiria que a ordem antiga -- 4 Hz primeiro -- continuava
    // valendo, que e exatamente o defeito recem-consertado. Log que mente
    // sobre a propria sequencia e pior que log nenhum.
    // ⚠️ A asserção filtrava pelas mensagens que JA diziam "sonda" -- e a
    // mutacao mostrou que, trocando o rotulo de volta, a mensagem deixava de
    // casar com o filtro e o teste passava sem checar nada. O que vale e a
    // ORDEM: nenhuma linha pode falar em 250 ms antes do CFG-PRT.
    // O `engole` come a fase RAPIDA inteira, e com isso a fase INSISTENTE
    // roda -- que e onde o rotulo errado morava. Foi o que a mutacao cobrou:
    // com `baud_exigido` a sondagem acertava de primeira e a linha mutada nem
    // era executada. Em funcao de `kSondagensPorBaud` para nao voltar a
    // passar vazio se a sondagem ganhar outra tentativa.
    Bancada b;
    b.uart.engole = kSondagensPorBaud * 2;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    ASSERT_TRUE(b.log.contem(Nivel::Warning, "insistindo nos dois"))
        << "a fase insistente nao rodou; o teste nao exercita o rotulo";
    bool passou_do_prt = false;
    for (const auto& e : b.log.entradas()) {
        if (e.mensagem.find("CFG-PRT") != std::string::npos) {
            passou_do_prt = true;
        }
        if (passou_do_prt) { continue; }
        EXPECT_EQ(e.mensagem.find("250 ms"), std::string::npos)
            << "anunciou 4 Hz antes de subir o baud: " << e.mensagem;
    }
    EXPECT_TRUE(passou_do_prt) << "o CFG-PRT nem apareceu no log";
}

TEST(ConfiguradorGps, o_log_diz_em_que_porta_o_modulo_apareceu) {
    // O desfecho da sondagem e a informacao que falta; o ACK em si o
    // `comanda` ja registrou, e repeti-lo era ruido.
    Bancada b;
    b.uart.baud_exigido = kBaudDesejado;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    bool achou = false;
    for (const auto& e : b.log.entradas()) {
        if (e.mensagem.find("modulo encontrado em 115200") !=
            std::string::npos) {
            achou = true;
        }
    }
    EXPECT_TRUE(achou);
}

}  // namespace
