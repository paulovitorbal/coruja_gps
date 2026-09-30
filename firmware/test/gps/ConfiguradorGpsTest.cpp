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
    /// Ids de CFG a que o módulo simplesmente não responde.
    std::vector<std::uint8_t> muda;
    /// Quantas respostas engolir antes de começar a responder (testa
    /// retentativa).
    unsigned engole = 0;
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
    }

    bool mandou(std::uint8_t id) const {
        for (const auto& c : comandos) {
            if (c.classe == ubx::kClasseCfg && c.id == id) { return true; }
        }
        return false;
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
        for (const auto id : muda) { if (id == c.id) { return; } }
        bool nak = false;
        for (const auto id : recusa) { if (id == c.id) { nak = true; } }

        if (intercala_nmea) {
            const char* s = "$GNRMC,123519.00,V,,,,,,,220926,,,N*63\r\n";
            for (const char* p = s; *p != 0; ++p) {
                pendente_.push_back(static_cast<std::uint8_t>(*p));
            }
        }
        const std::uint8_t id_confirmado =
            responde_pelo_comando_errado
                ? static_cast<std::uint8_t>(c.id ^ 0x7FU) : c.id;
        const std::uint8_t carga[2] = {c.classe, id_confirmado};
        std::uint8_t quadro[16];
        const std::size_t n = ubx::monta(ubx::kClasseAck,
                                         nak ? ubx::kAckNak : ubx::kAckAck,
                                         carga, sizeof carga, quadro,
                                         sizeof quadro);
        for (std::size_t i = 0; i < n; ++i) { pendente_.push_back(quadro[i]); }
    }

    std::vector<std::uint8_t> pendente_;
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

TEST(ConfiguradorGps, pede_250_ms_que_sao_os_4_hz_do_rf01_4) {
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    for (const auto& c : b.uart.comandos) {
        if (c.id != ubx::kCfgRate) { continue; }
        ASSERT_GE(c.payload.size(), 2U);
        const unsigned ms = c.payload[0] | (c.payload[1] << 8);
        EXPECT_EQ(ms, 250U);
        return;
    }
    FAIL() << "nao mandou CFG-RATE";
}

TEST(ConfiguradorGps, liga_a_RMC_e_desliga_as_outras_seis) {
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    unsigned ligadas = 0, desligadas = 0;
    for (const auto& c : b.uart.comandos) {
        if (c.id != ubx::kCfgMsg) { continue; }
        ASSERT_EQ(c.payload.size(), 3U);
        EXPECT_EQ(c.payload[0], ubx::kClasseNmea);
        if (c.payload[2] == 0) { ++desligadas; }
        else { ++ligadas; EXPECT_EQ(c.payload[1], ubx::kNmeaRmc); }
    }
    EXPECT_EQ(ligadas, 1U);
    EXPECT_EQ(desligadas, 6U) << "a GSV sozinha ja estoura a linha a 9600";
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
    // E o comportamento real: depois do CFG-PRT o modulo so fala no baud
    // novo. O CFG-CFG adiante e quem prova que a porta nova esta de pe.
    Bancada b;
    b.uart.baud_exigido = kBaudDesejado;
    // Os comandos ate o CFG-PRT ficam sem resposta, entao a configuracao
    // falha antes -- e este teste verifica justamente isso: sem o baud certo
    // desde o inicio, nao se chega ao fim.
    EXPECT_EQ(b.roda(), ResultadoConfigGps::SemResposta);
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

TEST(ConfiguradorGps, silencio_e_repetido_uma_vez) {
    Bancada b;
    b.uart.muda = {ubx::kCfgRate, ubx::kCfgMsg, ubx::kCfgPrt, ubx::kCfgCfg};
    b.roda();
    EXPECT_EQ(b.uart.quantos(ubx::kCfgRate), kTentativasPorComando);
}

TEST(ConfiguradorGps, recupera_na_segunda_tentativa) {
    Bancada b;
    b.uart.engole = 1;   // a primeira resposta some
    EXPECT_EQ(b.roda(), ResultadoConfigGps::Configurado);
    EXPECT_EQ(b.uart.quantos(ubx::kCfgRate), 2U);
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

}  // namespace
