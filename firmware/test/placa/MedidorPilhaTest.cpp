#include "placa/MedidorPilha.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

using namespace coruja;

TEST(MedidorPilha, pilha_toda_pintada_esta_toda_livre) {
    std::vector<std::uint32_t> area(64, kTintaDaPilha);
    EXPECT_EQ(palavras_intocadas(area.data(), area.size()), 64U);
}

TEST(MedidorPilha, conta_ate_a_PRIMEIRA_palavra_diferente) {
    // A pilha cresce para baixo: o que foi usado está no FIM do vetor, e a
    // área nunca tocada é a do começo. Varrer do outro lado daria o
    // complemento -- um número plausível e exatamente invertido.
    std::vector<std::uint32_t> area(64, kTintaDaPilha);
    area[40] = 0xDEADBEEF;   // o ponto mais fundo já alcançado
    EXPECT_EQ(palavras_intocadas(area.data(), area.size()), 40U);
}

TEST(MedidorPilha, um_unico_rabisco_no_comeco_zera_a_folga) {
    std::vector<std::uint32_t> area(64, kTintaDaPilha);
    area[0] = 0;
    EXPECT_EQ(palavras_intocadas(area.data(), area.size()), 0U);
}

TEST(MedidorPilha, tinta_que_reaparece_depois_nao_conta) {
    // Dado de verdade pode calhar de ser igual à tinta. A varredura para na
    // primeira diferença e não volta -- contar as ocorrências soltas
    // superestimaria a folga, que é o lado errado para errar.
    std::vector<std::uint32_t> area(64, 0x11111111);
    area[0] = kTintaDaPilha;
    area[50] = kTintaDaPilha;
    EXPECT_EQ(palavras_intocadas(area.data(), area.size()), 1U);
}

TEST(MedidorPilha, sem_tinta_nenhuma_a_folga_e_zero) {
    // É o caso que importa denunciar: a pilha foi usada até o fundo, e o
    // pico real é desconhecido -- pode ter passado dele.
    std::vector<std::uint32_t> area(64, 0xABCDEF01);
    EXPECT_EQ(palavras_intocadas(area.data(), area.size()), 0U);
}

TEST(MedidorPilha, area_vazia_e_ponteiro_nulo_nao_quebram) {
    EXPECT_EQ(palavras_intocadas(nullptr, 100), 0U);
    std::uint32_t nada = kTintaDaPilha;
    EXPECT_EQ(palavras_intocadas(&nada, 0), 0U);
}

TEST(MedidorPilha, a_tinta_nao_e_zero_nem_0xFF) {
    // Os dois aparecem naturalmente em dado de verdade, e contá-los como
    // "nunca usado" subestimaria o consumo.
    EXPECT_NE(kTintaDaPilha, 0x00000000U);
    EXPECT_NE(kTintaDaPilha, 0xFFFFFFFFU);
}

}  // namespace
