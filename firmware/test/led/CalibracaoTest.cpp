#include "led/Calibracao.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;
using namespace coruja::calibracao;

// Estes testes fixam os valores MEDIDOS contra alteração acidental. Não
// verificam física — verificam que ninguém mexeu sem medir de novo.

TEST(Calibracao, AsQuatroCoresDeViaSaoDistintas) {
    const Cor todas[] = {kSegura, kAmbar, kRosa, kPerigo};
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = i + 1; j < 4; ++j) {
            EXPECT_NE(todas[i], todas[j]) << "cores " << i << " e " << j;
        }
    }
}

TEST(Calibracao, OsValoresSaoOsMedidosEm19de09) {
    EXPECT_EQ(kAmbar, (Cor{255, 50, 0}));
    EXPECT_EQ(kRosa, (Cor{255, 0, 40}));
    EXPECT_EQ(kResistorVermelhoOhms, 330u);
    EXPECT_EQ(kResistorVerdeOhms, 470u);
    EXPECT_EQ(kResistorAzulOhms, 150u);
}

TEST(Calibracao, AsCompostasMantemOVermelhoEmCemPorCento) {
    // É a construção que o modo de calibração usa: vermelho fixo, o outro
    // canal variável.
    EXPECT_EQ(kAmbar.r, 255);
    EXPECT_EQ(kRosa.r, 255);
}

TEST(Calibracao, OAmbarNaoTemAzulEORosaNaoTemVerde) {
    // Misturar os três canais daria um branco sujo em vez de uma cor.
    EXPECT_EQ(kAmbar.b, 0);
    EXPECT_EQ(kRosa.g, 0);
}

TEST(Calibracao, ORosaSeAfastaDoVermelhoOSuficienteParaSerVisto) {
    // O critério do RF03.4 é perceptual e foi verificado a olho; aqui só se
    // garante que o canal azul não foi zerado por engano, o que tornaria o
    // rosa idêntico ao vermelho.
    EXPECT_GT(kRosa.b, 0) << "o rosa virou vermelho puro";
    EXPECT_GT(kAmbar.g, 0) << "o ambar virou vermelho puro";
}

}  // namespace
