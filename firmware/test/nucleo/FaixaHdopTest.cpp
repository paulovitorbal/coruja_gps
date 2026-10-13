#include "nucleo/FaixaHdop.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

TEST(FaixaHdop, as_cinco_faixas_da_convencao) {
    // Convenção usada pela indústria de GNSS, e não números inventados aqui:
    // <1 ideal, 1-2 excelente, 2-5 bom, 5-10 moderado, >10 ruim.
    EXPECT_EQ(classifica_hdop(0.8F), FaixaHdop::Ideal);
    EXPECT_EQ(classifica_hdop(1.5F), FaixaHdop::Excelente);
    EXPECT_EQ(classifica_hdop(3.0F), FaixaHdop::Bom);
    EXPECT_EQ(classifica_hdop(7.0F), FaixaHdop::Moderado);
    EXPECT_EQ(classifica_hdop(20.0F), FaixaHdop::Ruim);
}

TEST(FaixaHdop, as_bordas_pertencem_a_faixa_de_CIMA) {
    // 1,0 exato é "excelente", não "ideal". A escolha é arbitrária, mas tem
    // de ser uma só: duas bordas discordando fariam o indicador piscar entre
    // faixas com o HDOP parado.
    EXPECT_EQ(classifica_hdop(1.0F), FaixaHdop::Excelente);
    EXPECT_EQ(classifica_hdop(2.0F), FaixaHdop::Bom);
    EXPECT_EQ(classifica_hdop(5.0F), FaixaHdop::Moderado);
    EXPECT_EQ(classifica_hdop(10.0F), FaixaHdop::Ruim);
}

TEST(FaixaHdop, hdop_zero_nao_e_perfeicao) {
    // O M8N emite zero quando não tem solução. Tratar como "ideal" seria
    // afirmar a melhor precisão possível justamente quando não há nenhuma --
    // e é o pior erro que este indicador poderia cometer.
    EXPECT_EQ(classifica_hdop(0.0F), FaixaHdop::Desconhecida);
}

TEST(FaixaHdop, hdop_negativo_e_desconhecido) {
    EXPECT_EQ(classifica_hdop(-1.0F), FaixaHdop::Desconhecida);
}

TEST(FaixaHdop, as_barras_crescem_com_a_QUALIDADE_e_nao_com_o_numero) {
    // HDOP menor é melhor, e barra cheia tem de significar "bom". Inverter
    // isto daria um indicador que mostra tudo cheio quando o sinal é péssimo.
    EXPECT_GT(barras_de_hdop(FaixaHdop::Ideal),
              barras_de_hdop(FaixaHdop::Ruim));
    EXPECT_EQ(barras_de_hdop(FaixaHdop::Ideal), kBarrasHdop);
    EXPECT_EQ(barras_de_hdop(FaixaHdop::Desconhecida), 0);
}

TEST(FaixaHdop, cada_faixa_tem_um_numero_de_barras_proprio) {
    // Duas faixas com a mesma altura seriam indistinguíveis na tela, e a
    // distinção é a razão de o indicador existir.
    const int b[] = {barras_de_hdop(FaixaHdop::Ideal),
                     barras_de_hdop(FaixaHdop::Excelente),
                     barras_de_hdop(FaixaHdop::Bom),
                     barras_de_hdop(FaixaHdop::Moderado),
                     barras_de_hdop(FaixaHdop::Ruim)};
    for (unsigned i = 1; i < 5; ++i) {
        EXPECT_LT(b[i], b[i - 1]) << "faixa " << i;
    }
}

TEST(FaixaHdop, toda_faixa_tem_descricao_e_elas_diferem) {
    const FaixaHdop todas[] = {FaixaHdop::Desconhecida, FaixaHdop::Ideal,
                               FaixaHdop::Excelente, FaixaHdop::Bom,
                               FaixaHdop::Moderado, FaixaHdop::Ruim};
    for (const auto a : todas) {
        EXPECT_STRNE(descreve(a), nullptr);
        for (const auto b : todas) {
            if (a != b) { EXPECT_STRNE(descreve(a), descreve(b)); }
        }
    }
}

}  // namespace
