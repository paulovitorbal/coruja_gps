#include "nucleo/VelocidadeCruzeiro.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

/// Roda `segundos` de amostras a 4 Hz, que é a taxa do RF01.4.
void anda(VelocidadeCruzeiro& c, float kmh, std::uint32_t de_ms,
          std::uint32_t segundos) {
    for (std::uint32_t k = 0; k < segundos * 4U; ++k) {
        c.anota(kmh, de_ms + k * 250U);
    }
}

TEST(VelocidadeCruzeiro, comeca_sem_nada_a_dizer) {
    VelocidadeCruzeiro c;
    EXPECT_FALSE(c.valida());
}

TEST(VelocidadeCruzeiro, precisa_aquecer_antes_de_valer) {
    VelocidadeCruzeiro c;
    anda(c, 80.0F, 1000, 10);
    EXPECT_FALSE(c.valida()) << "dez segundos nao descrevem uma via";
}

TEST(VelocidadeCruzeiro, depois_do_aquecimento_vale_e_acerta) {
    VelocidadeCruzeiro c;
    anda(c, 80.0F, 1000, 30);
    ASSERT_TRUE(c.valida());
    EXPECT_NEAR(c.kmh(), 80.0F, 1.0F);
}

TEST(VelocidadeCruzeiro, uma_freada_curta_nao_derruba_a_media) {
    // É o ponto da constante de tempo longa: o cruzeiro descreve a VIA, e a
    // via não muda porque o motorista tirou o pé por dois segundos.
    VelocidadeCruzeiro c;
    anda(c, 80.0F, 1000, 30);
    anda(c, 50.0F, 31000, 2);
    EXPECT_TRUE(c.valida());
    EXPECT_GT(c.kmh(), 75.0F);
}

TEST(VelocidadeCruzeiro, parar_apaga_tudo) {
    VelocidadeCruzeiro c;
    anda(c, 80.0F, 1000, 30);
    ASSERT_TRUE(c.valida());
    c.anota(0.0F, 31000);
    EXPECT_FALSE(c.valida()) << "carro parado nao esta em via nenhuma";
}

TEST(VelocidadeCruzeiro, a_fronteira_dos_trinta_km_h) {
    VelocidadeCruzeiro c;
    anda(c, 80.0F, 1000, 30);
    ASSERT_TRUE(c.valida());
    c.anota(kVelocidadeMinimaCruzeiroKmh, 31000);
    EXPECT_TRUE(c.valida()) << "a fronteira e inclusiva";
    c.anota(kVelocidadeMinimaCruzeiroKmh - 0.1F, 31250);
    EXPECT_FALSE(c.valida());
}

TEST(VelocidadeCruzeiro, buraco_no_sinal_quebra_a_continuidade) {
    // Sem fix por tempo demais o veículo pode ter mudado de via, e a média de
    // antes passaria a mentir com cara de certeza.
    VelocidadeCruzeiro c;
    anda(c, 80.0F, 1000, 30);
    ASSERT_TRUE(c.valida());
    // A última amostra de `anda(..., 1000, 30)` cai em 30 750 ms.
    c.anota(80.0F, 30750U + kBuracoMaximoCruzeiroMs + 1U);
    EXPECT_FALSE(c.valida());
}

TEST(VelocidadeCruzeiro, buraco_curto_nao_quebra) {
    VelocidadeCruzeiro c;
    anda(c, 80.0F, 1000, 30);
    ASSERT_TRUE(c.valida());
    c.anota(80.0F, 30750U + kBuracoMaximoCruzeiroMs - 1U);
    EXPECT_TRUE(c.valida());
}

TEST(VelocidadeCruzeiro, a_taxa_do_receptor_nao_muda_a_constante_de_tempo) {
    // α = Δt/(τ+Δt) existe para isto: 1 Hz e 4 Hz têm de chegar ao mesmo
    // lugar depois do mesmo TEMPO. Com α fixo por amostra, o de 1 Hz ficaria
    // quatro vezes mais lento e o cruzeiro mentiria em taxa degradada.
    VelocidadeCruzeiro rapida;
    VelocidadeCruzeiro lenta;
    for (std::uint32_t k = 0; k < 40U * 4U; ++k) {
        rapida.anota(100.0F, 1000 + k * 250U);
    }
    for (std::uint32_t k = 0; k < 40U; ++k) { lenta.anota(100.0F, 1000 + k * 1000U); }
    ASSERT_TRUE(rapida.valida());
    ASSERT_TRUE(lenta.valida());
    EXPECT_NEAR(rapida.kmh(), lenta.kmh(), 1.0F);
}

TEST(VelocidadeCruzeiro, atravessa_o_estouro_do_relogio) {
    VelocidadeCruzeiro c;
    const std::uint32_t quase = 0xFFFFFF00U;
    for (std::uint32_t k = 0; k < 30U * 4U; ++k) {
        c.anota(80.0F, quase + k * 250U);   // estoura no meio
    }
    EXPECT_TRUE(c.valida());
    EXPECT_NEAR(c.kmh(), 80.0F, 1.0F);
}

/// A rampa de entrada na via rápida, medida em 28/10/2026.
///
/// São as velocidades de uma viagem gravada, uma a cada 6 s: um
/// minuto e pouco a ~57 km/h, a queda para 48,8 na alça, e a aceleração até
/// 80,9. O alarme que originou o pedido aconteceu no fim desta lista.
///
/// ⚠️ **Só velocidades.** Nenhuma coordenada entra num arquivo versionado.
const float kRampaReal[] = {61.6F, 57.8F, 58.4F, 58.8F, 58.6F, 56.4F, 55.8F,
                            58.0F, 59.5F, 58.4F, 52.8F, 48.8F, 54.9F, 64.3F,
                            72.3F, 78.2F, 80.9F, 77.8F, 78.3F};

TEST(VelocidadeCruzeiro, a_rampa_de_entrada_na_via_rapida) {
    // 🔴 O teste que fixou a constante de tempo. Com 20 s a média marcava
    // 72,45 no instante do alarme — teto 79,7, e o limite de 80 ficava
    // 0,3 km/h de fora. O desempate do RF03.11 não chegava a tempo
    // exatamente na entrada da via, que é quando ele faz falta.
    VelocidadeCruzeiro c;
    std::uint32_t agora = 1000;
    for (const float v : kRampaReal) {
        for (int k = 0; k < 24; ++k) {        // 6 s a 4 Hz
            c.anota(v, agora);
            agora += 250U;
        }
    }
    ASSERT_TRUE(c.valida());
    EXPECT_GE(c.kmh() * kFolgaDoLimite, 80.0F)
        << "o limite de 80 tem de caber no teto na entrada da via rapida";
}

TEST(VelocidadeCruzeiro, a_rampa_nao_promove_o_limite_seguinte) {
    // O contrapeso do teste acima: afrouxar a constante até o 80 caber é
    // fácil; o que não pode acontecer é o teto subir tanto que um radar de
    // 100 na mesma janela também passe a ser "a via".
    VelocidadeCruzeiro c;
    std::uint32_t agora = 1000;
    for (const float v : kRampaReal) {
        for (int k = 0; k < 24; ++k) {
            c.anota(v, agora);
            agora += 250U;
        }
    }
    EXPECT_LT(c.kmh() * kFolgaDoLimite, 100.0F);
}

TEST(VelocidadeCruzeiro, arrancada_curta_nao_promove_a_via) {
    // 4 s a 100 saindo de 60 movem a média 25% do caminho. Se isto passasse,
    // bastaria pisar fundo por um instante para calar o radar da marginal.
    VelocidadeCruzeiro c;
    anda(c, 60.0F, 1000, 40);
    const float antes = c.kmh();
    anda(c, 100.0F, 41000, 4);
    EXPECT_LT(c.kmh(), antes + 12.0F);
    EXPECT_LT(c.kmh() * kFolgaDoLimite, 80.0F) << "4 s nao fazem uma via de 80";
}

TEST(VelocidadeCruzeiro, esquece_zera) {
    VelocidadeCruzeiro c;
    anda(c, 80.0F, 1000, 30);
    ASSERT_TRUE(c.valida());
    c.esquece();
    EXPECT_FALSE(c.valida());
}

}  // namespace
