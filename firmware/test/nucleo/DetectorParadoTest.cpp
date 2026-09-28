#include "nucleo/DetectorParado.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

TEST(DetectorParado, NaoEstaParadoAntesDosTresSegundos) {
    DetectorParado d;
    d.atualiza(0.0F, 1000);
    d.atualiza(0.0F, 3999);
    EXPECT_FALSE(d.parado());
    d.atualiza(0.0F, 4000);
    EXPECT_TRUE(d.parado()) << "3 s exatos ja contam";
}

TEST(DetectorParado, AcimaDoLimiarNaoConta) {
    DetectorParado d;
    d.atualiza(3.0F, 0);  // o limiar e exclusivo: 3 km/h ainda e andar
    d.atualiza(3.0F, 5000);
    EXPECT_FALSE(d.parado());
}

TEST(DetectorParado, LogoAbaixoDoLimiarConta) {
    DetectorParado d;
    d.atualiza(2.9F, 0);
    d.atualiza(2.9F, 3000);
    EXPECT_TRUE(d.parado());
}

TEST(DetectorParado, AndarDeNovoDerrubaNaHora) {
    // Sair tarde deixaria o menu aberto com o carro ja andando.
    DetectorParado d;
    d.atualiza(0.0F, 0);
    d.atualiza(0.0F, 3000);
    ASSERT_TRUE(d.parado());
    d.atualiza(10.0F, 3001);
    EXPECT_FALSE(d.parado());
}

TEST(DetectorParado, PararDeNovoRecomecaAContagem) {
    DetectorParado d;
    d.atualiza(0.0F, 0);
    d.atualiza(0.0F, 3000);
    d.atualiza(10.0F, 3100);
    d.atualiza(0.0F, 3200);
    EXPECT_FALSE(d.parado()) << "herdou a contagem antiga";
    d.atualiza(0.0F, 6200);
    EXPECT_TRUE(d.parado());
}

// --- sem fix: os dois casos ---

TEST(DetectorParado, SemFixDesdeOBootContaComoParado) {
    // Bancada ou garagem coberta: nao ha o que alertar, e e onde se quer
    // mexer nos ajustes.
    DetectorParado d;
    d.sem_fix(0);
    EXPECT_FALSE(d.parado());
    d.sem_fix(3000);
    EXPECT_TRUE(d.parado());
}

TEST(DetectorParado, PerderOFixAndandoNaoParaOCarro) {
    // Um tunel a 80 km/h.
    DetectorParado d;
    d.atualiza(80.0F, 0);
    d.sem_fix(1000);
    d.sem_fix(10000);
    EXPECT_FALSE(d.parado()) << "deduziu que parou porque o sinal sumiu";
}

TEST(DetectorParado, PerderOFixParadoMantemParado) {
    DetectorParado d;
    d.atualiza(0.0F, 0);
    d.atualiza(0.0F, 3000);
    ASSERT_TRUE(d.parado());
    d.sem_fix(4000);
    EXPECT_TRUE(d.parado());
}

TEST(DetectorParado, SemFixNaoAdiantaAContagemDeQuemJaTeveFix) {
    // Estava andando ha 1 s, perde o fix: nao pode virar parado sozinho.
    DetectorParado d;
    d.atualiza(50.0F, 0);
    for (std::uint32_t t = 100; t <= 20000; t += 100) {
        d.sem_fix(t);
    }
    EXPECT_FALSE(d.parado());
}

TEST(DetectorParado, OFixDeVoltaVoltaAMandar) {
    DetectorParado d;
    d.atualiza(80.0F, 0);
    d.sem_fix(5000);
    d.atualiza(0.0F, 6000);
    EXPECT_FALSE(d.parado());
    d.atualiza(0.0F, 9000);
    EXPECT_TRUE(d.parado());
}

TEST(DetectorParado, ReiniciaVoltaAoBoot) {
    DetectorParado d;
    d.atualiza(80.0F, 0);
    d.reinicia();
    d.sem_fix(0);
    d.sem_fix(3000);
    EXPECT_TRUE(d.parado()) << "reinicia nao esqueceu que houve fix";
}

}  // namespace
