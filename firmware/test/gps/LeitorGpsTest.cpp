#include "gps/LeitorGps.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using namespace coruja;

/// Simula o receptor entregando bytes: aceita texto e binário, e devolve em
/// pedaços do tamanho pedido — porque na UART de verdade uma sentença chega
/// partida entre duas leituras, e é aí que um montador de linha quebra.
class SimuladorUart : public Uart {
public:
    std::vector<std::uint8_t> saida;
    std::size_t pedaco = 256;   ///< quantos bytes por `le()`

    void injeta(const std::string& s) {
        saida.insert(saida.end(), s.begin(), s.end());
    }
    void injeta(const std::vector<std::uint8_t>& b) {
        saida.insert(saida.end(), b.begin(), b.end());
    }

    void escreve(const std::uint8_t*, std::size_t) override {}
    void define_baud(std::uint32_t) override {}
    std::size_t le(std::uint8_t* destino, std::size_t capacidade) override {
        const std::size_t teto = capacidade < pedaco ? capacidade : pedaco;
        const std::size_t n = saida.size() < teto ? saida.size() : teto;
        for (std::size_t i = 0; i < n; ++i) { destino[i] = saida[i]; }
        saida.erase(saida.begin(), saida.begin() + static_cast<long>(n));
        return n;
    }
};

// Sentenças com o checksum CALCULADO, não suposto — a mesma disciplina do
// NmeaTest, onde checksums escritos à mão foram recusados pelo validador.
constexpr const char* kRmcValida =
    "$GNRMC,123519.00,A,1947.99496,S,04401.26264,W,22.4,84.4,220926,,,A*46\r\n";
constexpr const char* kRmcSemFix =
    "$GNRMC,123519.00,V,,,,,,,220926,,,N*63\r\n";

std::string com_checksum(const std::string& corpo) {
    std::uint8_t cs = 0;
    for (std::size_t i = 1; i < corpo.size(); ++i) {
        cs ^= static_cast<std::uint8_t>(corpo[i]);
    }
    char fim[8];
    std::snprintf(fim, sizeof fim, "*%02X\r\n", cs);
    return corpo + fim;
}

struct Bancada {
    SimuladorUart uart;
    LeitorGps     leitor{uart};
};

// ===================================================== leitura de telemetria

TEST(LeitorGps, sentenca_valida_vira_telemetria) {
    Bancada b;
    b.uart.injeta(kRmcValida);
    b.leitor.atualiza(0);
    EXPECT_EQ(b.leitor.fixes(), 1U);
    EXPECT_TRUE(b.leitor.tem_fix(0));
    EXPECT_NEAR(b.leitor.telemetria().velocidade_kmh, 22.4F * 1.852F, 0.1F);
    EXPECT_LT(b.leitor.telemetria().lat, 0.0F) << "latitude sul";
}

TEST(LeitorGps, sentenca_partida_entre_duas_leituras_e_remontada) {
    // Na UART de verdade a sentenca chega em pedacos. Um montador que
    // esperasse a linha inteira numa leitura perderia quase todas.
    Bancada b;
    b.uart.injeta(kRmcValida);
    b.uart.pedaco = 7;            // sete bytes por vez
    for (int i = 0; i < 20; ++i) { b.leitor.atualiza(0); }
    EXPECT_EQ(b.leitor.fixes(), 1U);
}

TEST(LeitorGps, aceita_linha_sem_o_retorno_de_carro) {
    Bancada b;
    std::string s = kRmcValida;
    s.erase(s.size() - 2, 1);     // tira o \r, deixa o \n
    b.uart.injeta(s);
    b.leitor.atualiza(0);
    EXPECT_EQ(b.leitor.fixes(), 1U);
}

TEST(LeitorGps, o_retorno_de_carro_no_fim_e_tolerado_pelo_parser) {
    // O leitor entrega a linha COM o \r, porque o analisa_rmc para no `*`.
    // Fica afirmado aqui em vez de presumido: se um dia o parser deixar de
    // tolerar, este teste avisa, e nao o comportamento em campo.
    Bancada b;
    b.uart.injeta(kRmcValida);            // termina em \r\n
    b.leitor.atualiza(0);
    EXPECT_EQ(b.leitor.fixes(), 1U);
}

TEST(LeitorGps, varias_sentencas_no_mesmo_bloco) {
    Bancada b;
    for (int i = 0; i < 5; ++i) { b.uart.injeta(kRmcValida); }
    b.leitor.atualiza(0);
    EXPECT_EQ(b.leitor.fixes(), 5U);
}

// ========================================== sem fix nao e erro de leitura

TEST(LeitorGps, sentenca_sem_fix_nao_conta_como_fix_nem_como_erro) {
    // O receptor esta falando e dizendo que nao sabe onde esta. Contar isso
    // como checksum invalido mandaria procurar ruido eletrico onde so ha
    // ceu encoberto.
    Bancada b;
    b.uart.injeta(kRmcSemFix);
    b.leitor.atualiza(0);
    EXPECT_EQ(b.leitor.fixes(), 0U);
    EXPECT_EQ(b.leitor.sem_fix(), 1U);
    EXPECT_EQ(b.leitor.monitor().checksums_invalidos(), 0U);
    EXPECT_FALSE(b.leitor.tem_fix(0));
}

TEST(LeitorGps, sem_fix_nao_apaga_a_ultima_telemetria_boa) {
    Bancada b;
    b.uart.injeta(kRmcValida);
    b.leitor.atualiza(0);
    const float v = b.leitor.telemetria().velocidade_kmh;
    b.uart.injeta(kRmcSemFix);
    b.leitor.atualiza(250);
    EXPECT_FLOAT_EQ(b.leitor.telemetria().velocidade_kmh, v);
}

// ==================================================== RF01.5: contagens

TEST(LeitorGps, checksum_invalido_e_contado_a_parte) {
    Bancada b;
    std::string ruim = kRmcValida;
    ruim[10] = '9';               // muda um digito, o checksum nao fecha mais
    b.uart.injeta(ruim);
    b.leitor.atualiza(0);
    EXPECT_EQ(b.leitor.fixes(), 0U);
    EXPECT_EQ(b.leitor.monitor().checksums_invalidos(), 1U);
}

TEST(LeitorGps, sentenca_de_outro_tipo_e_ignorada_em_silencio) {
    Bancada b;
    b.uart.injeta(com_checksum("$GNGGA,123519.00,1947.9949,S,04401.2626,W,1,08"));
    b.leitor.atualiza(0);
    EXPECT_EQ(b.leitor.fixes(), 0U);
    EXPECT_EQ(b.leitor.monitor().checksums_invalidos(), 0U)
        << "GGA nao e defeito: e so outra sentenca";
}

TEST(LeitorGps, bytes_de_UBX_nao_viram_erro_de_checksum) {
    // ESTE e o ponto. Quadro binario lido como texto produz "linhas" sem $,
    // e conta-las poluiria justamente o numero que separa "o GPS nao esta
    // enviando" de "estamos falhando em interpretar" -- fazendo a
    // configuracao do modulo parecer ruido eletrico na linha.
    std::uint8_t quadro[32];
    const std::uint8_t carga[2] = {ubx::kClasseCfg, ubx::kCfgRate};
    const std::size_t n = ubx::monta(ubx::kClasseAck, ubx::kAckAck, carga,
                                     sizeof carga, quadro, sizeof quadro);
    Bancada b;
    b.uart.injeta(std::vector<std::uint8_t>(quadro, quadro + n));
    b.uart.injeta("\n");          // força o fecho de linha
    b.leitor.atualiza(0);
    EXPECT_EQ(b.leitor.monitor().checksums_invalidos(), 0U);
}

TEST(LeitorGps, o_ACK_do_UBX_continua_visivel_no_meio_do_NMEA) {
    // A configuracao acontece com o modulo ja emitindo sentencas. O leitor
    // tem de enxergar as duas coisas.
    std::uint8_t quadro[32];
    const std::uint8_t carga[2] = {ubx::kClasseCfg, ubx::kCfgRate};
    const std::size_t n = ubx::monta(ubx::kClasseAck, ubx::kAckAck, carga,
                                     sizeof carga, quadro, sizeof quadro);
    Bancada b;
    b.uart.injeta(kRmcValida);
    b.uart.injeta(std::vector<std::uint8_t>(quadro, quadro + n));
    b.uart.injeta(kRmcValida);
    b.leitor.atualiza(0);
    EXPECT_EQ(b.leitor.fixes(), 2U);
    EXPECT_EQ(b.leitor.ubx().classe_alvo(), ubx::kClasseCfg);
    EXPECT_EQ(b.leitor.ubx().id_alvo(), ubx::kCfgRate);
}

TEST(LeitorGps, linha_sem_fim_e_descartada_em_vez_de_prender_o_buffer) {
    Bancada b;
    b.uart.injeta(std::string(500, 'x'));   // sem \n
    b.leitor.atualiza(0);
    // Conta LINHAS descartadas, nao bytes: 500 bytes sem fim sao ~3 buffers
    // cheios. Contar por byte daria 336 e transformaria o diagnostico num
    // numero sem sentido.
    EXPECT_GE(b.leitor.linhas_descartadas(), 1U);
    EXPECT_LE(b.leitor.linhas_descartadas(), 5U);
    // E o leitor segue funcionando depois.
    b.uart.injeta(kRmcValida);
    b.leitor.atualiza(250);
    EXPECT_EQ(b.leitor.fixes(), 1U);
}

// ============================================= integração com o RF01.5

TEST(LeitorGps, quatro_hz_leva_o_monitor_a_nominal) {
    Bancada b;
    for (std::uint32_t t = 0; t <= 3000; t += 250) {
        b.uart.injeta(kRmcValida);
        b.leitor.atualiza(t);
    }
    EXPECT_EQ(b.leitor.monitor().estado(), EstadoTaxa::Nominal);
    EXPECT_NEAR(b.leitor.monitor().taxa_hz(), 4.0F, 0.1F);
}

TEST(LeitorGps, o_silencio_derruba_a_taxa_mesmo_sem_byte_algum) {
    // A avaliacao roda a cada volta, inclusive sem dado. E a janela
    // esvaziando que faz a taxa cair; sem isso o silencio nunca apareceria.
    Bancada b;
    for (std::uint32_t t = 0; t <= 3000; t += 250) {
        b.uart.injeta(kRmcValida);
        b.leitor.atualiza(t);
    }
    ASSERT_EQ(b.leitor.monitor().estado(), EstadoTaxa::Nominal);
    for (std::uint32_t t = 3050; t <= 9000; t += 50) { b.leitor.atualiza(t); }
    EXPECT_EQ(b.leitor.monitor().estado(), EstadoTaxa::Falha);
}

TEST(LeitorGps, o_fix_envelhece) {
    Bancada b;
    b.uart.injeta(kRmcValida);
    b.leitor.atualiza(1000);
    EXPECT_TRUE(b.leitor.tem_fix(1999));
    EXPECT_FALSE(b.leitor.tem_fix(2000));
}

TEST(LeitorGps, o_envelhecimento_atravessa_o_estouro_do_relogio) {
    constexpr std::uint32_t kQuase = 0xFFFFFF00U;
    Bancada b;
    b.uart.injeta(kRmcValida);
    b.leitor.atualiza(kQuase);
    EXPECT_TRUE(b.leitor.tem_fix(static_cast<std::uint32_t>(kQuase + 500U)));
    EXPECT_FALSE(b.leitor.tem_fix(static_cast<std::uint32_t>(kQuase + 1500U)));
}

TEST(LeitorGps, reinicia_zera_tudo) {
    Bancada b;
    b.uart.injeta(kRmcValida);
    b.leitor.atualiza(0);
    ASSERT_EQ(b.leitor.fixes(), 1U);
    b.leitor.reinicia();
    EXPECT_EQ(b.leitor.fixes(), 0U);
    EXPECT_FALSE(b.leitor.tem_fix(0));
}

}  // namespace
