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

    /// O que o leitor MANDA para o receptor -- as consultas de diagnóstico
    /// são a única coisa que ele escreve.
    std::vector<std::uint8_t> escritos;
    void escreve(const std::uint8_t* dados, std::size_t n) override {
        escritos.insert(escritos.end(), dados, dados + n);
    }
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

// ============================= diagnostico: por que o fix demorou 2:48

/// Monta um quadro UBX completo, com checksum, para injetar na porta.
std::vector<std::uint8_t> quadro_ubx(std::uint8_t classe, std::uint8_t id,
                                     const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> q = {
        0xB5, 0x62, classe, id,
        static_cast<std::uint8_t>(payload.size() & 0xFF),
        static_cast<std::uint8_t>(payload.size() >> 8)};
    q.insert(q.end(), payload.begin(), payload.end());
    std::uint8_t a = 0, b2 = 0;
    for (std::size_t i = 2; i < q.size(); ++i) {
        a = static_cast<std::uint8_t>(a + q[i]);
        b2 = static_cast<std::uint8_t>(b2 + a);
    }
    q.push_back(a);
    q.push_back(b2);
    return q;
}

TEST(LeitorGps, a_consulta_de_diagnostico_pede_as_quatro_mensagens) {
    // ⚠️ A quarta entrou em 16/10/2026: `primeiro fix em 6.0 s (5 sat, hdop
    // 5.8)` levantou a suspeita de que o limite e SINAL, e nao orbita. Sem
    // medir C/N0 nao da para decidir entre investir em antena ou em
    // assistencia -- e assistencia nao conserta sinal fraco.
    Bancada b;
    b.leitor.pergunta_diagnostico();
    // Classe e id de cada consulta, na ordem em que saem.
    const std::vector<std::pair<std::uint8_t, std::uint8_t>> esperadas = {
        {ubx::kClasseNav, ubx::kNavStatus},
        {ubx::kClasseMon, ubx::kMonHw},
        {ubx::kClasseNav, ubx::kNavAopStatus},
        {ubx::kClasseNav, ubx::kNavSat}};
    ASSERT_EQ(b.uart.escritos.size(), esperadas.size() * 8)
        << "consulta tem 8 bytes: cabecalho e checksum, sem payload";
    for (std::size_t i = 0; i < esperadas.size(); ++i) {
        EXPECT_EQ(b.uart.escritos[i * 8 + 2], esperadas[i].first);
        EXPECT_EQ(b.uart.escritos[i * 8 + 3], esperadas[i].second);
    }
}

TEST(LeitorGps, a_resposta_de_nav_status_vira_o_ttff_do_modulo) {
    // ⚠️ Este numero e o do RECEPTOR. O nosso conta desde o boot do Pico e
    // inclui a configuracao; divergirem aponta o atraso para o nosso lado.
    Bancada b;
    std::vector<std::uint8_t> p(16, 0);
    p[4] = 3;                   // 3D
    p[5] = 0x01;                // gpsFixOK
    p[8] = 0x10; p[9] = 0x92; p[10] = 0x02;   // ttff = 168464 ms
    b.uart.injeta(quadro_ubx(ubx::kClasseNav, ubx::kNavStatus, p));
    b.leitor.atualiza(1000);

    EXPECT_TRUE(b.leitor.navegacao().teve_fix);
    EXPECT_EQ(b.leitor.navegacao().ttff_ms, 168464U);
    EXPECT_EQ(b.leitor.navegacao().tipo_fix, 3);
    EXPECT_EQ(b.leitor.respostas_diagnostico(), 1U);
}

TEST(LeitorGps, a_resposta_de_mon_hw_vira_ruido_e_interferencia) {
    Bancada b;
    std::vector<std::uint8_t> p(60, 0);
    p[16] = 85;      // noisePerMS
    p[18] = 0x08; p[19] = 0x07;  // agcCnt = 1800
    p[20] = 2;       // antena ok
    p[22] = 0x08;    // jammingState = 2
    p[45] = 42;      // jamInd
    b.uart.injeta(quadro_ubx(ubx::kClasseMon, ubx::kMonHw, p));
    b.leitor.atualiza(1000);

    EXPECT_EQ(b.leitor.radio().ruido, 85);
    EXPECT_EQ(b.leitor.radio().agc, 1800);
    EXPECT_EQ(b.leitor.radio().antena, AntenaGps::Ok);
    EXPECT_EQ(b.leitor.radio().interferencia, 42);
    EXPECT_EQ(b.leitor.radio().jamming, Jamming::Aviso);
}

TEST(LeitorGps, o_diagnostico_atravessa_sentencas_NMEA_na_mesma_linha) {
    // Na linha de verdade o quadro binario chega ENTRE sentencas de texto, e
    // e por isso que o leitor de UBX e de fluxo.
    Bancada b;
    std::vector<std::uint8_t> p(16, 0);
    p[4] = 0x01;   // aopCfg: useAOP
    p[5] = 0x00;   // ocioso, que e o estado normal de quem esta em dia
    b.uart.injeta(kRmcValida);
    b.uart.injeta(quadro_ubx(ubx::kClasseNav, ubx::kNavAopStatus, p));
    b.uart.injeta(kRmcSemFix);
    b.leitor.atualiza(1000);

    EXPECT_TRUE(b.leitor.aop().ligado);
    EXPECT_FALSE(b.leitor.aop().calculando);
    EXPECT_EQ(b.leitor.fixes(), 1U) << "a sentenca de texto se perdeu";
}

TEST(LeitorGps, quadro_UBX_de_outra_mensagem_nao_conta_como_resposta) {
    // ⚠️ Quem pergunta espera TRES respostas e relata quando elas chegam.
    // Contar um quadro qualquer faria o relato sair cedo, com o conteudo da
    // consulta anterior -- numero velho apresentado como medida de agora.
    Bancada b;
    b.uart.injeta(quadro_ubx(ubx::kClasseNav, 0x07, std::vector<std::uint8_t>(92, 0)));
    b.leitor.atualiza(1000);
    EXPECT_EQ(b.leitor.respostas_diagnostico(), 0U);
}

TEST(LeitorGps, resposta_curta_demais_nao_conta_nem_preenche) {
    // Payload curto e outra versao de protocolo ou dessincronia. Decodificar
    // o que nao existe daria ruido com cara de medida.
    Bancada b;
    b.uart.injeta(quadro_ubx(ubx::kClasseMon, ubx::kMonHw,
                             std::vector<std::uint8_t>(20, 0xAB)));
    b.leitor.atualiza(1000);
    EXPECT_EQ(b.leitor.respostas_diagnostico(), 0U);
    EXPECT_EQ(b.leitor.radio().ruido, 0);
}

TEST(LeitorGps, quadro_de_diagnostico_corrompido_nao_vira_diagnostico) {
    Bancada b;
    std::vector<std::uint8_t> p(60, 0);
    p[45] = 200;
    auto q = quadro_ubx(ubx::kClasseMon, ubx::kMonHw, p);
    q[q.size() - 1] ^= 0xFF;   // estraga o checksum
    b.uart.injeta(q);
    b.leitor.atualiza(1000);
    EXPECT_EQ(b.leitor.respostas_diagnostico(), 0U);
    EXPECT_EQ(b.leitor.radio().interferencia, 0);
}

// ================================= RF01.5: o tunel nao e defeito de config

TEST(LeitorGps, um_tunel_nao_pede_reconfiguracao_do_receptor) {
    // O aparelho entra em falha de taxa dentro de qualquer tunel -- sem fix,
    // a taxa cai a zero. Reenviar a sequencia UBX ali reescreveria a
    // configuracao do receptor, CFG-CFG incluido, a cada viaduto do caminho.
    Bancada b;
    for (std::uint32_t t = 0; t <= 4000; t += 250) {
        b.uart.injeta(kRmcValida);
        b.leitor.atualiza(t);
    }
    ASSERT_EQ(b.leitor.monitor().estado(), EstadoTaxa::Nominal);

    for (std::uint32_t t = 4250; t <= 16000; t += 250) {
        b.uart.injeta(kRmcSemFix);
        b.leitor.atualiza(t);
    }
    ASSERT_EQ(b.leitor.monitor().estado(), EstadoTaxa::Falha);
    EXPECT_FALSE(b.leitor.monitor().precisa_reconfigurar())
        << "o receptor esta falando: falta ceu, nao configuracao";
}

TEST(LeitorGps, o_receptor_que_emudece_pede_reconfiguracao) {
    Bancada b;
    for (std::uint32_t t = 0; t <= 4000; t += 250) {
        b.uart.injeta(kRmcValida);
        b.leitor.atualiza(t);
    }
    // Silencio na linha: nem fix, nem "sem solucao", nem lixo.
    for (std::uint32_t t = 4250; t <= 16000; t += 250) {
        b.leitor.atualiza(t);
    }
    ASSERT_EQ(b.leitor.monitor().estado(), EstadoTaxa::Falha);
    EXPECT_TRUE(b.leitor.monitor().precisa_reconfigurar());
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

// ======================================================= qualidade do fix ==
//
// A RMC diz ONDE o receptor acha que está; a GGA diz QUANTO isso vale. Sem a
// segunda, um fix de 3 m e um de 60 m chegam ao aparelho com a mesma cara.

namespace {

std::string gga(const std::string& qualidade = "1",
                const std::string& satelites = "09",
                const std::string& hdop = "0.92") {
    return com_checksum(
        "$GNGGA,123519.00,1947.99496,S,04401.26264,W," + qualidade + "," +
        satelites + "," + hdop + ",545.4,M,46.9,M,,") + "\r\n";
}

}  // namespace

TEST(LeitorGps, a_GGA_preenche_a_qualidade) {
    Bancada b;
    b.uart.injeta(gga());
    b.leitor.atualiza(1000);
    EXPECT_TRUE(b.leitor.tem_qualidade(1000));
    EXPECT_EQ(b.leitor.qualidade().satelites, 9);
    EXPECT_NEAR(b.leitor.qualidade().hdop, 0.92F, 1e-4F);
}

TEST(LeitorGps, a_GGA_NAO_conta_como_fix) {
    // O monitor de taxa do RF01.5 conta ÉPOCAS DE NAVEGAÇÃO, e quem as marca
    // é a RMC. Contar a GGA dobraria a taxa medida e esconderia um receptor
    // caído pela metade.
    Bancada b;
    b.uart.injeta(gga());
    b.leitor.atualiza(1000);
    EXPECT_FALSE(b.leitor.tem_fix(1000));
}

TEST(LeitorGps, a_GGA_nao_atrapalha_a_RMC_que_vem_junto) {
    Bancada b;
    b.uart.injeta(gga());
    b.uart.injeta(kRmcValida);
    b.leitor.atualiza(1000);
    EXPECT_TRUE(b.leitor.tem_fix(1000));
    EXPECT_TRUE(b.leitor.tem_qualidade(1000));
    EXPECT_EQ(b.leitor.qualidade().satelites, 9);
}

TEST(LeitorGps, a_qualidade_ENVELHECE_como_o_fix) {
    // ⚠️ Esta é a armadilha de emparelhar duas sentenças: sem carimbo
    // próprio, um HDOP de dez minutos atrás seria exibido ao lado de uma
    // posição recente, afirmando precisão que já não existe.
    Bancada b;
    b.uart.injeta(gga());
    b.leitor.atualiza(1000);
    ASSERT_TRUE(b.leitor.tem_qualidade(1000));
    EXPECT_FALSE(b.leitor.tem_qualidade(1000 + kFixVelhoMs));
}

TEST(LeitorGps, GGA_sem_solucao_nao_apaga_a_qualidade_anterior) {
    Bancada b;
    b.uart.injeta(gga("1", "11", "0.80"));
    b.leitor.atualiza(1000);
    b.uart.injeta(gga("0", "00", "99.99"));
    b.leitor.atualiza(1250);
    EXPECT_EQ(b.leitor.qualidade().satelites, 11);
    EXPECT_NEAR(b.leitor.qualidade().hdop, 0.80F, 1e-4F);
}

TEST(LeitorGps, GGA_corrompida_conta_no_checksum_como_qualquer_sentenca) {
    // Não é regressão: `analisa_rmc` já devolvia `ChecksumInvalido` para
    // QUALQUER sentença mal fechada, muito antes de olhar se era RMC. Ligar
    // a GGA não muda a regra -- só dá mais sentenças para ela valer.
    Bancada b;
    std::string ruim = gga();
    ruim[ruim.size() - 3] = (ruim[ruim.size() - 3] == '0') ? '1' : '0';
    b.uart.injeta(ruim);
    b.leitor.atualiza(1000);
    EXPECT_GT(b.leitor.monitor().checksums_invalidos(), 0U);
    EXPECT_FALSE(b.leitor.tem_qualidade(1000));
}

TEST(LeitorGps, reinicia_esquece_a_qualidade) {
    Bancada b;
    b.uart.injeta(gga());
    b.leitor.atualiza(1000);
    ASSERT_TRUE(b.leitor.tem_qualidade(1000));
    b.leitor.reinicia();
    EXPECT_FALSE(b.leitor.tem_qualidade(1000));
}
