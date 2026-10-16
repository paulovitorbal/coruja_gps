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

// ===================================================== NAV-SAT: o SINAL
//
// ⚠️ **Existe por causa de `primeiro fix em 6.0 s (5 sat, hdop 5.8)`.** Cinco
// satelites e HDOP 5,8 sao fracos -- ceu aberto da 8 a 12 e HDOP abaixo de
// 1,5. A tabela do datasheet promete 26 s de cold start a -130 dBm, e a
// antena patch dentro da caixa, na garagem sob sombrite, nao esta a -130 dBm.
//
// Isso decide o proximo passo do projeto: assistencia (AssistNow, RTC) entrega
// a ORBITA, mas o receptor ainda precisa OUVIR o satelite. Com C/N0 baixo, o
// dinheiro e o firmware da assistencia nao compram os 2 s prometidos. Medir
// antes de comprar.

TEST(UbxDiagnostico, nav_sat_resume_o_melhor_sinal_e_quantos_prestam) {
    // Cinco satelites: 44, 38, 22, 36 e um EXATAMENTE no limiar. Quatro
    // passam -- o limiar e inclusivo, e sem um satelite em cima dele nada
    // distinguiria `>=` de `>`.
    Payload<8 + 12 * 5> p;
    p.b[5] = 5;                       // numSvs
    const std::uint8_t cnos[] = {44, 38, 22, 36, kCnoUtil};
    for (std::size_t i = 0; i < 5; ++i) {
        p.b[8 + i * 12 + 2] = cnos[i];                  // cno
        p.b[8 + i * 12 + 8] = 0x08;                     // flags: svUsed
    }
    EstadoSinal s;
    ASSERT_TRUE(analisa_nav_sat(p.b, sizeof p.b, &s));
    EXPECT_TRUE(s.respondeu);
    EXPECT_EQ(s.vistos, 5);
    EXPECT_EQ(s.melhor_cno, 44);
    EXPECT_EQ(s.bons, 4);             // >= kCnoUtil, inclusivo
    EXPECT_EQ(s.usados, 5);
    EXPECT_FALSE(s.truncado);
}

TEST(UbxDiagnostico, nav_sat_conta_como_usado_so_quem_tem_o_bit) {
    // ⚠️ "Rastreado" e "usado na solucao" sao coisas diferentes, e a
    // diferenca entre eles e meio diagnostico: ver oito e usar quatro aponta
    // para geometria ou para sinal no limiar, nao para falta de satelite.
    Payload<8 + 12 * 3> p;
    p.b[5] = 3;
    for (std::size_t i = 0; i < 3; ++i) { p.b[8 + i * 12 + 2] = 40; }
    p.b[8 + 0 * 12 + 8] = 0x08;       // so o primeiro entrou na solucao
    EstadoSinal s;
    ASSERT_TRUE(analisa_nav_sat(p.b, sizeof p.b, &s));
    EXPECT_EQ(s.vistos, 3);
    EXPECT_EQ(s.usados, 1);
}

TEST(UbxDiagnostico, nav_sat_sem_satelite_nenhum_e_resposta_valida) {
    // Zero satelite e uma MEDIDA -- e a mais eloquente de todas numa
    // partida que nao fixa. Nao pode ser confundida com "nao respondeu".
    Payload<8> p;
    p.b[5] = 0;
    EstadoSinal s;
    ASSERT_TRUE(analisa_nav_sat(p.b, sizeof p.b, &s));
    EXPECT_TRUE(s.respondeu);
    EXPECT_EQ(s.vistos, 0);
    EXPECT_EQ(s.melhor_cno, 0);
}

TEST(UbxDiagnostico, nav_sat_truncado_diz_quantos_HAVIA_e_que_foi_cortado) {
    // ⚠️ O quadro completo passa de 300 bytes com o ceu cheio, e o leitor
    // guarda menos que isso. O numero de satelites vem do CABECALHO, entao
    // continua certo; o resumo e dos que couberam, e a bandeira avisa.
    constexpr std::size_t kCabem = 5;
    Payload<8 + 12 * kCabem> p;
    p.b[5] = 24;                      // o modulo viu 24...
    for (std::size_t i = 0; i < kCabem; ++i) {
        p.b[8 + i * 12 + 2] = static_cast<std::uint8_t>(30 + i);
    }
    EstadoSinal s;
    ASSERT_TRUE(analisa_nav_sat(p.b, sizeof p.b, &s));
    EXPECT_EQ(s.vistos, 24) << "o total tem de vir do cabecalho";
    EXPECT_TRUE(s.truncado);
    EXPECT_EQ(s.melhor_cno, 34);      // o melhor dos que couberam
}

TEST(UbxDiagnostico, nav_sat_curto_demais_e_recusado) {
    Payload<6> p;
    EstadoSinal s;
    EXPECT_FALSE(analisa_nav_sat(p.b, sizeof p.b, &s));
    EXPECT_FALSE(s.respondeu);
}

TEST(UbxDiagnostico, a_linha_traz_o_sinal_quando_ele_respondeu) {
    EstadoNavegacao nav; nav.respondeu = true;
    EstadoRadio radio;   radio.respondeu = true;
    EstadoAop aop;       aop.respondeu = true;
    EstadoSinal sinal;
    sinal.respondeu = true; sinal.vistos = 9; sinal.usados = 5;
    sinal.melhor_cno = 41; sinal.bons = 4;

    char linha[kTamDiagnostico] = {};
    ASSERT_GT(formata_diagnostico(nav, radio, aop, sinal, linha, sizeof linha),
              0u);
    EXPECT_NE(std::strstr(linha, "9 vistos"), nullptr);
    EXPECT_NE(std::strstr(linha, "5 usados"), nullptr);
    EXPECT_NE(std::strstr(linha, "41"), nullptr);
}

TEST(UbxDiagnostico, sem_resposta_do_nav_sat_a_linha_diz_isso) {
    EstadoNavegacao nav; nav.respondeu = true;
    EstadoRadio radio;   radio.respondeu = true;
    EstadoAop aop;       aop.respondeu = true;
    EstadoSinal sinal;   // respondeu = false

    char linha[kTamDiagnostico] = {};
    formata_diagnostico(nav, radio, aop, sinal, linha, sizeof linha);
    EXPECT_NE(std::strstr(linha, "NAV-SAT sem resposta"), nullptr);
    EXPECT_EQ(std::strstr(linha, "0 vistos"), nullptr);
}

// ================================================== a linha do diagnostico
//
// ⚠️ **Esta secao existe por causa da partida de 15/10/2026.** O aparelho
// gravou, numa partida fria de verdade:
//
//     radio: ruido 0 | agc 0/8191 | antena iniciando | interferencia 0/255
//
// Nada daquilo foi medido. O `MON-HW` nao respondeu dentro dos tres segundos
// de teto -- a linha saiu exatamente 3000 ms depois do fix, esgotando a
// espera -- e o que foi impresso era a estrutura zerada. `agc 0/8191` le-se
// como front-end morto, e nao havia nada de errado com o front-end.
//
// Formatar aqui, e nao no `main.cpp`, e o que torna isso testavel: o laco
// principal nao tem teste, e foi justamente ali que o defeito morou.

TEST(UbxDiagnostico, a_linha_traz_os_tres_quando_os_tres_responderam) {
    EstadoNavegacao nav;
    nav.respondeu = true; nav.teve_fix = true;
    nav.ttff_ms = 400; nav.desde_boot_ms = 5500;
    EstadoRadio radio;
    radio.respondeu = true; radio.ruido = 90; radio.agc = 1716;
    radio.antena = AntenaGps::Ok; radio.interferencia = 5;
    EstadoAop aop;
    aop.respondeu = true; aop.ligado = true;

    EstadoSinal sinal; sinal.respondeu = true;
    char linha[kTamDiagnostico] = {};
    ASSERT_GT(formata_diagnostico(nav, radio, aop, sinal, linha, sizeof linha), 0u);
    EXPECT_NE(std::strstr(linha, "ruido 90"), nullptr);
    EXPECT_NE(std::strstr(linha, "agc 1716/8191"), nullptr);
    EXPECT_NE(std::strstr(linha, "antena ok"), nullptr);
    EXPECT_NE(std::strstr(linha, "0.4 s"), nullptr);
    EXPECT_NE(std::strstr(linha, "aop ligado"), nullptr);
}

TEST(UbxDiagnostico, sem_resposta_do_mon_hw_a_linha_diz_isso_e_nao_zeros) {
    // O caso de 15/10/2026, e o unico teste que realmente importa nesta
    // secao: numero nenhum pode sair de uma estrutura que ninguem preencheu.
    EstadoNavegacao nav;
    nav.respondeu = true; nav.teve_fix = true; nav.ttff_ms = 38200;
    EstadoRadio radio;   // respondeu = false
    EstadoAop aop;
    aop.respondeu = true; aop.ligado = true;

    EstadoSinal sinal; sinal.respondeu = true;
    char linha[kTamDiagnostico] = {};
    ASSERT_GT(formata_diagnostico(nav, radio, aop, sinal, linha, sizeof linha), 0u);
    EXPECT_NE(std::strstr(linha, "MON-HW sem resposta"), nullptr);
    EXPECT_EQ(std::strstr(linha, "agc"), nullptr);
    EXPECT_EQ(std::strstr(linha, "ruido"), nullptr);
    EXPECT_EQ(std::strstr(linha, "antena"), nullptr);
    // O que respondeu continua sendo relatado.
    EXPECT_NE(std::strstr(linha, "38.2 s"), nullptr);
}

TEST(UbxDiagnostico, sem_resposta_do_nav_status_nao_inventa_ttff) {
    EstadoNavegacao nav;   // respondeu = false
    EstadoRadio radio;
    radio.respondeu = true; radio.antena = AntenaGps::Ok;
    EstadoAop aop;
    aop.respondeu = true;

    EstadoSinal sinal; sinal.respondeu = true;
    char linha[kTamDiagnostico] = {};
    formata_diagnostico(nav, radio, aop, sinal, linha, sizeof linha);
    EXPECT_NE(std::strstr(linha, "NAV-STATUS sem resposta"), nullptr);
    EXPECT_EQ(std::strstr(linha, "ttff do modulo 0.0"), nullptr);
}

TEST(UbxDiagnostico, sem_resposta_do_aopstatus_nao_afirma_desligado) {
    // ⚠️ "DESLIGADO" e uma AFIRMACAO sobre a configuracao do modulo. Dizer
    // isso porque a pergunta nao voltou mandaria procurar defeito onde nao
    // ha.
    EstadoNavegacao nav; nav.respondeu = true;
    EstadoRadio radio;   radio.respondeu = true;
    EstadoAop aop;       // respondeu = false

    EstadoSinal sinal; sinal.respondeu = true;
    char linha[kTamDiagnostico] = {};
    formata_diagnostico(nav, radio, aop, sinal, linha, sizeof linha);
    EXPECT_NE(std::strstr(linha, "NAV-AOPSTATUS sem resposta"), nullptr);
    EXPECT_EQ(std::strstr(linha, "aop DESLIGADO"), nullptr);
}

TEST(UbxDiagnostico, a_linha_traz_o_relogio_do_proprio_modulo) {
    // Sem isto, a diferenca entre o nosso cronometro e o dele -- 7,2 s na
    // partida de 15/10 -- so da para inferir. Com o `msss` ao lado, ela fica
    // aritmetica: o modulo diz ha quanto tempo ELE esta ligado.
    EstadoNavegacao nav;
    nav.respondeu = true; nav.teve_fix = true;
    nav.ttff_ms = 38200; nav.desde_boot_ms = 44900;
    EstadoRadio radio; radio.respondeu = true;
    EstadoAop aop;     aop.respondeu = true;

    EstadoSinal sinal; sinal.respondeu = true;
    char linha[kTamDiagnostico] = {};
    formata_diagnostico(nav, radio, aop, sinal, linha, sizeof linha);
    EXPECT_NE(std::strstr(linha, "44.9 s"), nullptr);
}

TEST(UbxDiagnostico, ainda_sem_fix_e_diferente_de_sem_resposta) {
    EstadoNavegacao nav;
    nav.respondeu = true;   // o modulo respondeu...
    nav.teve_fix = false;   // ...dizendo que ainda nao fixou
    EstadoRadio radio; radio.respondeu = true;
    EstadoAop aop;     aop.respondeu = true;

    EstadoSinal sinal; sinal.respondeu = true;
    char linha[kTamDiagnostico] = {};
    formata_diagnostico(nav, radio, aop, sinal, linha, sizeof linha);
    EXPECT_NE(std::strstr(linha, "ainda nao fixou"), nullptr);
    EXPECT_EQ(std::strstr(linha, "NAV-STATUS sem resposta"), nullptr);
}

TEST(UbxDiagnostico, nenhum_respondeu_ainda_produz_linha) {
    // Linha nenhuma seria o pior desfecho para algo que existe para
    // explicar: quem le o diario precisa ver que a pergunta foi feita.
    EstadoNavegacao nav; EstadoRadio radio; EstadoAop aop; EstadoSinal sinal;
    char linha[kTamDiagnostico] = {};
    EXPECT_GT(formata_diagnostico(nav, radio, aop, sinal, linha, sizeof linha), 0u);
    EXPECT_NE(std::strstr(linha, "sem resposta"), nullptr);
}

TEST(UbxDiagnostico, buffer_curto_nao_estoura_e_devolve_zero) {
    EstadoNavegacao nav; EstadoRadio radio; EstadoAop aop;
    EstadoSinal sinal;
    char linha[8] = {};
    EXPECT_EQ(formata_diagnostico(nav, radio, aop, sinal, linha, sizeof linha), 0u);
}

// ============================ a bandeira de "esta estrutura foi preenchida"

TEST(UbxDiagnostico, decodificar_marca_a_estrutura_como_respondida) {
    Payload<16> p;
    EstadoNavegacao nav;
    ASSERT_FALSE(nav.respondeu);
    ASSERT_TRUE(analisa_nav_status(p.b, sizeof p.b, &nav));
    EXPECT_TRUE(nav.respondeu);

    Payload<60> q;
    EstadoRadio radio;
    ASSERT_TRUE(analisa_mon_hw(q.b, sizeof q.b, &radio));
    EXPECT_TRUE(radio.respondeu);

    Payload<16> r;
    EstadoAop aop;
    ASSERT_TRUE(analisa_nav_aopstatus(r.b, sizeof r.b, &aop));
    EXPECT_TRUE(aop.respondeu);
}

TEST(UbxDiagnostico, quadro_curto_demais_nao_marca_como_respondida) {
    Payload<8> curto;
    EstadoRadio radio;
    EXPECT_FALSE(analisa_mon_hw(curto.b, sizeof curto.b, &radio));
    EXPECT_FALSE(radio.respondeu);
}

TEST(UbxDiagnostico, aopstatus_curto_demais_e_recusado) {
    Payload<5> p;
    EstadoAop a{};
    EXPECT_FALSE(analisa_nav_aopstatus(p.b, sizeof p.b, &a));
}

}  // namespace
