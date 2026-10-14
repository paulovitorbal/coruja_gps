// As três mensagens que respondem "por que o fix demorou 2:48".
//
// Em 14/10/2026 o primeiro fix levou 2 min 48 s com o carro parado um minuto
// numa garagem descoberta — seis vezes o cold start do datasheet (26 s). As
// hipóteses que sobraram foram duas, e são opostas: o receptor parte sem dado
// nenhum, ou parte cego por ruído. Estas mensagens separam as duas, e é o
// próprio módulo quem responde.

#include "nucleo/UbxDiagnostico.h"

#include <gtest/gtest.h>

#include <cstring>

namespace {

using namespace coruja;

/// Monta um payload do tamanho certo, com tudo zerado. Os testes escrevem só
/// os campos que lhes interessam — como o módulo faz com os reservados.
template <std::size_t N>
struct Payload {
    std::uint8_t b[N] = {};
    void u16(std::size_t off, std::uint16_t v) {
        b[off] = static_cast<std::uint8_t>(v & 0xFF);
        b[off + 1] = static_cast<std::uint8_t>(v >> 8);
    }
    void u32(std::size_t off, std::uint32_t v) {
        for (std::size_t i = 0; i < 4; ++i) {
            b[off + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
        }
    }
};

// ============================================================== NAV-STATUS

TEST(UbxDiagnostico, nav_status_traz_o_ttff_medido_pelo_modulo) {
    // ⚠️ Este é o número que encerra a discussão: o nosso TTFF conta desde o
    // boot do Pico e inclui a configuracao do receptor; este conta do que o
    // RECEPTOR fez. Divergirem muito aponta o atraso para o nosso lado.
    Payload<16> p;
    p.b[4] = 3;          // gpsFix: 3D
    p.b[5] = 0x01;       // flags: gpsFixOK
    p.u32(8, 168'400);   // ttff
    p.u32(12, 171'000);  // msss

    EstadoNavegacao e{};
    ASSERT_TRUE(analisa_nav_status(p.b, sizeof p.b, &e));
    EXPECT_EQ(e.ttff_ms, 168'400U);
    EXPECT_EQ(e.tipo_fix, 3);
    EXPECT_TRUE(e.fix_valido);
    EXPECT_EQ(e.desde_boot_ms, 171'000U);
}

TEST(UbxDiagnostico, nav_status_sem_fix_tem_ttff_zero_e_isso_nao_e_erro) {
    // Zero aqui significa "ainda não houve fix nesta ligação", e não "fixou
    // instantaneamente". Quem exibir tem de saber a diferença.
    Payload<16> p;
    EstadoNavegacao e{};
    ASSERT_TRUE(analisa_nav_status(p.b, sizeof p.b, &e));
    EXPECT_EQ(e.ttff_ms, 0U);
    EXPECT_FALSE(e.fix_valido);
    EXPECT_FALSE(e.teve_fix);
}

TEST(UbxDiagnostico, nav_status_fix_sem_gpsFixOK_nao_conta_como_valido) {
    // O módulo pode reportar tipo 3 com a solução ainda fora dos limites de
    // DOP/precisão. Tratar isso como fix bom seria afirmar posição que o
    // próprio receptor não assina.
    Payload<16> p;
    p.b[4] = 3;
    p.b[5] = 0x00;
    EstadoNavegacao e{};
    ASSERT_TRUE(analisa_nav_status(p.b, sizeof p.b, &e));
    EXPECT_EQ(e.tipo_fix, 3);
    EXPECT_FALSE(e.fix_valido);
}

TEST(UbxDiagnostico, nav_status_curto_demais_e_recusado) {
    Payload<15> p;
    EstadoNavegacao e{};
    EXPECT_FALSE(analisa_nav_status(p.b, sizeof p.b, &e));
}

// ================================================================= MON-HW

TEST(UbxDiagnostico, mon_hw_traz_ruido_agc_antena_e_interferencia) {
    Payload<60> p;
    p.u16(16, 85);     // noisePerMS
    p.u16(18, 1800);   // agcCnt, de 0 a 8191
    p.b[20] = 2;       // aStatus: OK
    p.b[22] = 0x08;    // flags: jammingState = 2 (aviso), nos bits 2-3
    p.b[45] = 42;      // jamInd, de 0 a 255

    EstadoRadio r{};
    ASSERT_TRUE(analisa_mon_hw(p.b, sizeof p.b, &r));
    EXPECT_EQ(r.ruido, 85);
    EXPECT_EQ(r.agc, 1800);
    EXPECT_EQ(r.antena, AntenaGps::Ok);
    EXPECT_EQ(r.interferencia, 42);
    EXPECT_EQ(r.jamming, Jamming::Aviso);
}

TEST(UbxDiagnostico, mon_hw_le_o_jamInd_no_byte_45) {
    // ⚠️ O `jamInd` fica DEPOIS de 17 bytes de "VP" reservados, no byte 45.
    // É por causa dele que o leitor de UBX teve de passar a guardar mais que
    // os 8 bytes que o ACK precisa.
    Payload<60> p;
    p.b[45] = 255;
    EstadoRadio r{};
    ASSERT_TRUE(analisa_mon_hw(p.b, sizeof p.b, &r));
    EXPECT_EQ(r.interferencia, 255);
}

TEST(UbxDiagnostico, mon_hw_classifica_os_quatro_estados_da_antena) {
    const AntenaGps esperado[] = {AntenaGps::Iniciando, AntenaGps::Desconhecida,
                                  AntenaGps::Ok, AntenaGps::Curto,
                                  AntenaGps::Aberta};
    for (std::uint8_t i = 0; i < 5; ++i) {
        Payload<60> p;
        p.b[20] = i;
        EstadoRadio r{};
        ASSERT_TRUE(analisa_mon_hw(p.b, sizeof p.b, &r));
        EXPECT_EQ(r.antena, esperado[i]) << "aStatus " << int(i);
    }
}

TEST(UbxDiagnostico, mon_hw_com_estado_de_jamming_desconhecido) {
    // `0` nos bits 2-3 quer dizer que o monitor de interferência está
    // desligado — e não "não há interferência". A distinção importa: num o
    // número não foi medido, no outro foi e deu limpo.
    Payload<60> p;
    p.b[22] = 0x00;
    EstadoRadio r{};
    ASSERT_TRUE(analisa_mon_hw(p.b, sizeof p.b, &r));
    EXPECT_EQ(r.jamming, Jamming::Desconhecido);
}

TEST(UbxDiagnostico, mon_hw_curto_demais_e_recusado) {
    Payload<44> p;
    EstadoRadio r{};
    EXPECT_FALSE(analisa_mon_hw(p.b, sizeof p.b, &r));
}

// ========================================================== NAV-AOPSTATUS

TEST(UbxDiagnostico, aopstatus_diz_se_a_previsao_esta_ligada_e_rodando) {
    Payload<16> p;
    p.b[4] = 0x01;   // aopCfg: useAOP
    p.b[5] = 1;      // status != 0: calculando
    EstadoAop a{};
    ASSERT_TRUE(analisa_nav_aopstatus(p.b, sizeof p.b, &a));
    EXPECT_TRUE(a.ligado);
    EXPECT_TRUE(a.calculando);
}

TEST(UbxDiagnostico, aopstatus_ligado_mas_ocioso_e_o_estado_NORMAL) {
    // `status == 0` é o estado de repouso: ligado e sem cálculo pendente.
    // Ler isso como "não está funcionando" faria desligar o que funciona.
    Payload<16> p;
    p.b[4] = 0x01;
    p.b[5] = 0;
    EstadoAop a{};
    ASSERT_TRUE(analisa_nav_aopstatus(p.b, sizeof p.b, &a));
    EXPECT_TRUE(a.ligado);
    EXPECT_FALSE(a.calculando);
}

TEST(UbxDiagnostico, aopstatus_curto_demais_e_recusado) {
    Payload<5> p;
    EstadoAop a{};
    EXPECT_FALSE(analisa_nav_aopstatus(p.b, sizeof p.b, &a));
}

}  // namespace
