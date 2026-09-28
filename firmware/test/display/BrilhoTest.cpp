#include "display/Brilho.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

TEST(Brilho, comeca_no_maximo) {
    Brilho b;
    EXPECT_EQ(b.percentual(), 100);
}

TEST(Brilho, o_piso_e_5_por_cento_e_nunca_zero) {
    // Com o visor apagado o usuario perde a referencia visual para
    // recupera-lo: nao ha como achar o encoder no escuro sem feedback.
    Brilho b;
    for (int i = 0; i < 50; ++i) { b.diminui(); }
    EXPECT_EQ(b.percentual(), kBrilhoMinimoPct);
    EXPECT_GT(b.duty(), 0) << "chegou a apagar de verdade";
}

TEST(Brilho, o_teto_e_100_e_nao_passa) {
    Brilho b;
    for (int i = 0; i < 50; ++i) { b.aumenta(); }
    EXPECT_EQ(b.percentual(), kBrilhoMaximoPct);
}

TEST(Brilho, cada_clique_move_cinco_por_cento) {
    Brilho b;
    b.diminui();
    EXPECT_EQ(b.percentual(), 95);
    b.diminui();
    EXPECT_EQ(b.percentual(), 90);
    b.aumenta();
    EXPECT_EQ(b.percentual(), 95);
}

TEST(Brilho, sao_vinte_passos_do_piso_ao_teto) {
    Brilho b;
    for (int i = 0; i < 100; ++i) { b.diminui(); }
    int cliques = 0;
    while (b.percentual() < kBrilhoMaximoPct) { b.aumenta(); ++cliques; }
    EXPECT_EQ(cliques, static_cast<int>(kPassosBrilho) - 1);
}

TEST(Brilho, o_duty_cresce_sempre) {
    Brilho b;
    for (int i = 0; i < 100; ++i) { b.diminui(); }
    std::uint16_t anterior = 0;
    for (std::size_t i = 0; i < kPassosBrilho; ++i) {
        EXPECT_GT(b.duty(), anterior) << "no passo " << i;
        anterior = b.duty();
        b.aumenta();
    }
}

TEST(Brilho, a_curva_e_perceptual_e_nao_linear) {
    // ESTE e o ponto do requisito. Com passos lineares de duty, toda a
    // mudanca perceptivel acontece no fundo da escala e os dez cliques de
    // cima nao fazem nada. Na curva de 2,2, metade do brilho PERCEBIDO sao
    // ~22% do duty -- bem longe dos 50% que o linear daria.
    Brilho b;
    for (int i = 0; i < 100; ++i) { b.diminui(); }
    for (int i = 0; i < 9; ++i) { b.aumenta(); }    // 50%
    ASSERT_EQ(b.percentual(), 50);
    const double fracao = b.duty() / 65535.0;
    EXPECT_LT(fracao, 0.30) << "a curva achatou e virou quase linear";
    EXPECT_GT(fracao, 0.15) << "a curva exagerou e o meio da escala sumiu";
}

TEST(Brilho, o_piso_e_visivel_mas_discreto) {
    // 5% percebido sao ~0,14% do duty. Tem de ser maior que zero e bem
    // menor que 1% -- e o que permite dirigir a noite sem ofuscamento.
    Brilho b;
    for (int i = 0; i < 100; ++i) { b.diminui(); }
    EXPECT_GT(b.duty(), 0);
    EXPECT_LT(b.duty(), 655) << "o piso nao esta baixo o bastante para a noite";
}

TEST(Brilho, o_pwm_fica_acima_da_faixa_que_assobia_e_cintila) {
    EXPECT_GE(kFrequenciaPwmHz, 20000U);
}

}  // namespace
