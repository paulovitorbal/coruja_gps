// O anel entre a interrupcao da serial e o laco.
//
// 🔴 O FIFO de hardware do UART tem 32 bytes e transborda em 2,8 ms a
// 115200; um redesenho da velocidade bloqueia o SPI por ~53 ms. Sem o anel,
// a diferenca saia em fixes perdidos -- 3,7 Hz medidos em vez de 4,0.

#include "nucleo/AnelBytes.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

using coruja::AnelBytes;

std::vector<std::uint8_t> drena(AnelBytes& a, std::size_t quanto = 4096) {
    std::vector<std::uint8_t> saida(quanto);
    const auto n = a.le(saida.data(), quanto);
    saida.resize(n);
    return saida;
}

TEST(AnelBytes, nasce_vazio) {
    AnelBytes a;
    EXPECT_EQ(a.quantos(), 0u);
    EXPECT_EQ(a.perdidos(), 0u);
    EXPECT_TRUE(drena(a).empty());
}

TEST(AnelBytes, o_que_entra_sai_na_mesma_ordem) {
    AnelBytes a;
    for (std::uint8_t b : {1, 2, 3, 4, 5}) { ASSERT_TRUE(a.escreve(b)); }
    EXPECT_EQ(drena(a), (std::vector<std::uint8_t>{1, 2, 3, 4, 5}));
}

TEST(AnelBytes, leitura_parcial_preserva_o_resto) {
    AnelBytes a;
    for (std::uint8_t b : {1, 2, 3, 4, 5}) { a.escreve(b); }
    std::uint8_t dois[2] = {};
    EXPECT_EQ(a.le(dois, 2), 2u);
    EXPECT_EQ(dois[0], 1);
    EXPECT_EQ(dois[1], 2);
    EXPECT_EQ(drena(a), (std::vector<std::uint8_t>{3, 4, 5}));
}

TEST(AnelBytes, da_a_volta_sem_perder_nada) {
    // O caso que um indice mal fechado quebra: escrever e ler muito mais que
    // o tamanho do anel, sempre com folga.
    AnelBytes a;
    std::uint8_t esperado = 0;
    for (int volta = 0; volta < 50; ++volta) {
        for (int i = 0; i < 100; ++i) {
            ASSERT_TRUE(a.escreve(static_cast<std::uint8_t>(esperado + i)));
        }
        const auto saiu = drena(a);
        ASSERT_EQ(saiu.size(), 100u) << "volta " << volta;
        for (int i = 0; i < 100; ++i) {
            ASSERT_EQ(saiu[i], static_cast<std::uint8_t>(esperado + i));
        }
        esperado = static_cast<std::uint8_t>(esperado + 100);
    }
    EXPECT_EQ(a.perdidos(), 0u);
}

TEST(AnelBytes, cheio_descarta_o_NOVO_e_conta) {
    // ⚠️ Sobrescrever o antigo corromperia a sentenca que o laco esta no
    // meio de ler -- trocaria perda visivel por lixo que passa pelo parser.
    AnelBytes a;
    std::size_t couberam = 0;
    for (std::size_t i = 0; i < AnelBytes::kTamanho + 10; ++i) {
        if (a.escreve(static_cast<std::uint8_t>(i & 0xFF))) { ++couberam; }
    }
    EXPECT_EQ(couberam, AnelBytes::kTamanho - 1)
        << "o anel guarda tamanho-1: um lugar fica vago para separar cheio "
           "de vazio";
    EXPECT_EQ(a.perdidos(), 11u);
    // O que ja estava la continua intacto e na ordem.
    const auto saiu = drena(a);
    ASSERT_EQ(saiu.size(), AnelBytes::kTamanho - 1);
    EXPECT_EQ(saiu[0], 0);
    EXPECT_EQ(saiu[1], 1);
}

TEST(AnelBytes, depois_de_esvaziar_volta_a_aceitar) {
    AnelBytes a;
    while (a.escreve(0xAA)) {}
    ASSERT_GT(a.perdidos(), 0u);
    drena(a);
    EXPECT_TRUE(a.escreve(0xBB));
    EXPECT_EQ(drena(a), (std::vector<std::uint8_t>{0xBB}));
}

TEST(AnelBytes, o_contador_de_perdidos_pode_ser_zerado) {
    // O `vigia` relata por janela de cinco minutos, nao desde o boot.
    AnelBytes a;
    while (a.escreve(0)) {}
    ASSERT_GT(a.perdidos(), 0u);
    a.zera_perdidos();
    EXPECT_EQ(a.perdidos(), 0u);
}

TEST(AnelBytes, quantos_acompanha_o_enchimento) {
    AnelBytes a;
    EXPECT_EQ(a.quantos(), 0u);
    a.escreve(1); a.escreve(2); a.escreve(3);
    EXPECT_EQ(a.quantos(), 3u);
    std::uint8_t um[1];
    a.le(um, 1);
    EXPECT_EQ(a.quantos(), 2u);
}

TEST(AnelBytes, destino_nulo_nao_quebra) {
    AnelBytes a;
    a.escreve(1);
    EXPECT_EQ(a.le(nullptr, 10), 0u);
    EXPECT_EQ(a.quantos(), 1u);
}

TEST(AnelBytes, capacidade_zero_nao_consome) {
    AnelBytes a;
    a.escreve(1);
    std::uint8_t d[1];
    EXPECT_EQ(a.le(d, 0), 0u);
    EXPECT_EQ(a.quantos(), 1u);
}

TEST(AnelBytes, o_anel_cobre_o_pior_bloqueio_conhecido) {
    // ⚠️ Teste de DIMENSIONAMENTO, nao de logica. A 115200 chegam 11 520 B/s;
    // o pior bloqueio medido e o redesenho completo da faixa do numero,
    // ~53 ms. Se alguem encolher o anel abaixo disso, a perda volta -- e
    // volta silenciosa.
    constexpr double kBytesPorMs = 11520.0 / 1000.0;
    constexpr double kPiorBloqueioMs = 53.0;
    EXPECT_GT(AnelBytes::kTamanho, kBytesPorMs * kPiorBloqueioMs)
        << "o anel nao cobre um redesenho da faixa do numero";
}

}  // namespace
