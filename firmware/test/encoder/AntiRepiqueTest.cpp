#include "encoder/AntiRepique.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

TEST(AntiRepique, NaoAceitaAntesDaJanela) {
    AntiRepique f(5);
    EXPECT_FALSE(f.amostra(true, 0));
    EXPECT_FALSE(f.amostra(true, 4));
    EXPECT_FALSE(f.estavel());
}

TEST(AntiRepique, AceitaDepoisDaJanela) {
    AntiRepique f(5);
    EXPECT_FALSE(f.amostra(true, 0));
    EXPECT_TRUE(f.amostra(true, 5));
    EXPECT_TRUE(f.estavel());
}

TEST(AntiRepique, RepiqueDentroDaJanelaNaoGeraEvento) {
    AntiRepique f(5);
    f.amostra(true, 0);
    f.amostra(false, 1);   // soltou: candidato descartado
    f.amostra(true, 2);
    f.amostra(false, 3);
    EXPECT_FALSE(f.amostra(true, 4));
    EXPECT_FALSE(f.estavel());
}

TEST(AntiRepique, BotaoMantidoGeraUmEventoSo) {
    AntiRepique f(5);
    f.amostra(true, 0);
    EXPECT_TRUE(f.amostra(true, 10));
    for (std::uint32_t t = 11; t < 500; ++t) {
        EXPECT_FALSE(f.amostra(true, t)) << "gerou evento repetido em t=" << t;
    }
}

TEST(AntiRepique, SoltarNaoEEvento) {
    AntiRepique f(5);
    f.amostra(true, 0);
    ASSERT_TRUE(f.amostra(true, 10));
    f.amostra(false, 11);
    EXPECT_FALSE(f.amostra(false, 20));  // borda de saída não é clique
    EXPECT_FALSE(f.estavel());
}

TEST(AntiRepique, SegundoCliqueDepoisDeSoltarFunciona) {
    AntiRepique f(5);
    f.amostra(true, 0);
    ASSERT_TRUE(f.amostra(true, 10));
    f.amostra(false, 20);
    ASSERT_FALSE(f.amostra(false, 30));
    f.amostra(true, 40);
    EXPECT_TRUE(f.amostra(true, 50));
}

TEST(AntiRepique, SobreviveAoWraparoundDeTrintaEDoisBits) {
    // `time_us_32()` dá a volta a cada ~49 dias. Com subtração sem sinal a
    // conta continua certa, e um carro não pode receber clique fantasma.
    AntiRepique f(5);
    const std::uint32_t quase = 0xFFFFFFFEU;
    EXPECT_FALSE(f.amostra(true, quase));
    EXPECT_TRUE(f.amostra(true, quase + 5));  // dá a volta para 3
}

TEST(AntiRepique, JanelaZeroAceitaImediatamente) {
    AntiRepique f(0);
    EXPECT_TRUE(f.amostra(true, 100));
}

}  // namespace
