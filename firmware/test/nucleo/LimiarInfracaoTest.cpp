#include "nucleo/LimiarInfracao.h"

#include <gtest/gtest.h>

#include "nucleo/Ponto.h"

namespace {

using namespace coruja;

// --- V_infra: RF03.6, desconto de projeto de 6 km/h ate 100 e 5% acima ---

TEST(VelocidadeInfracao, AbaixoDe100SomaSeisKmh) {
    EXPECT_FLOAT_EQ(velocidade_infracao(30), 36.0F);
    EXPECT_FLOAT_EQ(velocidade_infracao(60), 66.0F);
    EXPECT_FLOAT_EQ(velocidade_infracao(80), 86.0F);
}

TEST(VelocidadeInfracao, EmExatamente100AindaEAbsoluto) {
    // A fronteira pertence a faixa absoluta: 100 + 6, nao 100 * 1,05.
    EXPECT_FLOAT_EQ(velocidade_infracao(100), 106.0F);
}

TEST(VelocidadeInfracao, AcimaDe100UsaCincoPorCento) {
    EXPECT_FLOAT_EQ(velocidade_infracao(110), 115.5F);
    EXPECT_FLOAT_EQ(velocidade_infracao(120), 126.0F);
}

TEST(VelocidadeInfracao, EMaisConservadorQueALei) {
    // A lei desconta 7 km/h e 7%; o projeto desconta 6 e 5%. O limiar do
    // projeto tem de vir ANTES do legal, nunca depois.
    for (std::uint8_t l : {30, 40, 50, 60, 70, 80, 90, 100}) {
        EXPECT_LT(velocidade_infracao(l), l + 7.0F) << "limite " << int(l);
    }
    for (std::uint8_t l : {110, 120}) {
        EXPECT_LT(velocidade_infracao(l), l * 1.07F) << "limite " << int(l);
    }
}

TEST(VelocidadeInfracao, SemLimiteNaoTemLimiar) {
    // Camera de semaforo: nao ha velocidade a comparar, e o chamador nunca
    // deve entrar em Zona de Perigo (RF03.3).
    EXPECT_FLOAT_EQ(velocidade_infracao(kSemLimite), 0.0F);
}

TEST(VelocidadeInfracao, EMonotonica) {
    float anterior = 0.0F;
    for (std::uint8_t l : {30, 40, 50, 60, 70, 80, 90, 100, 110, 120}) {
        const float v = velocidade_infracao(l);
        EXPECT_GT(v, anterior) << "limite " << int(l);
        anterior = v;
    }
}

// --- faixas do buzzer: RF03.7, ancoradas em V_infra e nao no limite (R-23) ---

TEST(ExcessoPercentual, AbaixoDoLimiarDaZero) {
    EXPECT_FLOAT_EQ(excesso_percentual(80.0F, 80), 0.0F);
    EXPECT_FLOAT_EQ(excesso_percentual(86.0F, 80), 0.0F);
}

TEST(ExcessoPercentual, DezPorCentoAcimaDoLimiar) {
    // V_infra de 80 e 86; 10% acima e 94,6.
    EXPECT_NEAR(excesso_percentual(94.6F, 80), 10.0F, 0.1F);
}

TEST(ExcessoPercentual, AsFaixasNaoFicamVaziasEmLimiteBaixo) {
    // Este e o R-23: ancorado no limite da via, 6 km/h sobre 30 ja seriam 20%,
    // e as faixas de 10% e 20% nunca seriam alcancadas. Ancorado em V_infra,
    // a faixa 1 existe de verdade.
    const float limiar = velocidade_infracao(30);  // 36
    EXPECT_FLOAT_EQ(excesso_percentual(limiar, 30), 0.0F);
    EXPECT_GT(excesso_percentual(limiar + 1.0F, 30), 0.0F);
    EXPECT_LT(excesso_percentual(limiar + 3.0F, 30), 10.0F);
}

TEST(ExcessoPercentual, SemLimiteDaZeroEmQualquerVelocidade) {
    EXPECT_FLOAT_EQ(excesso_percentual(200.0F, kSemLimite), 0.0F);
}

}  // namespace
