#include "buzzer/CadenciaBuzzer.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

using namespace coruja;

/// Roda a cadência de `de` a `ate` em passos de 1 ms e devolve quantos
/// milissegundos o buzzer passou ligado, e quantas vezes ele **ligou**.
struct Conta {
    std::uint32_t ms_ligado = 0;
    std::uint32_t bipes = 0;
};

Conta mede(CadenciaBuzzer* c, std::uint32_t de, std::uint32_t ate) {
    Conta r;
    // Comeca em `false` de proposito: a cadencia ja nasce ligada quando a
    // faixa e definida, e essa primeira borda E um bipe. Partir do estado
    // atual contaria um a menos em toda faixa.
    bool antes = false;
    for (std::uint32_t t = de; t <= ate; ++t) {
        const bool agora = c->atualiza(t);
        if (agora) { ++r.ms_ligado; }
        if (agora && !antes) { ++r.bipes; }
        antes = agora;
    }
    return r;
}

// ================================================ padrões da tabela do RF03.7

TEST(PadraoSonoro, valores_da_tabela_do_requisito) {
    EXPECT_EQ(padrao_de(FaixaSonora::Lenta).ligado_ms, 100U);
    EXPECT_EQ(padrao_de(FaixaSonora::Lenta).periodo_ms, 1000U);
    EXPECT_EQ(padrao_de(FaixaSonora::Rapida).ligado_ms, 100U);
    EXPECT_EQ(padrao_de(FaixaSonora::Rapida).periodo_ms, 350U);
    EXPECT_EQ(padrao_de(FaixaSonora::Pulso).ligado_ms, 50U);
    EXPECT_EQ(padrao_de(FaixaSonora::Pulso).periodo_ms, 100U);
}

TEST(PadraoSonoro, a_faixa_tres_e_pulso_e_nao_tom_continuo) {
    // Correção deliberada de 2026-09-18: tom contínuo é o padrão MENOS
    // detectável contra ruído estável, porque o ouvido se adapta a ele em
    // segundos — e vento faz exatamente isso. "Simplificar" a faixa 3 para
    // ligado permanente pioraria justamente o alerta mais grave.
    const PadraoSonoro p = padrao_de(FaixaSonora::Pulso);
    EXPECT_LT(p.ligado_ms, p.periodo_ms) << "faixa 3 virou tom continuo";
    EXPECT_EQ(p.periodo_ms - p.ligado_ms, 50U) << "tem de haver silencio";
}

TEST(PadraoSonoro, as_tres_faixas_escalonam_em_frequencia) {
    // 1 Hz -> ~2,9 Hz -> 10 Hz. Se duas faixas tiverem o mesmo periodo, o
    // escalonamento deixa de ser perceptivel e o requisito perde o sentido.
    EXPECT_GT(padrao_de(FaixaSonora::Lenta).periodo_ms,
              padrao_de(FaixaSonora::Rapida).periodo_ms);
    EXPECT_GT(padrao_de(FaixaSonora::Rapida).periodo_ms,
              padrao_de(FaixaSonora::Pulso).periodo_ms);
}

// ====================================================== cadência no tempo

TEST(CadenciaBuzzer, comeca_calada) {
    CadenciaBuzzer c;
    EXPECT_FALSE(c.ligado());
    EXPECT_FALSE(c.atualiza(0));
    EXPECT_EQ(c.faixa(), FaixaSonora::Nenhuma);
}

TEST(CadenciaBuzzer, silencio_nunca_liga) {
    CadenciaBuzzer c;
    c.define_faixa(FaixaSonora::Nenhuma, 0);
    EXPECT_EQ(mede(&c, 0, 5000).ms_ligado, 0U);
}

TEST(CadenciaBuzzer, faixa_lenta_da_um_bipe_de_100ms_por_segundo) {
    CadenciaBuzzer c;
    c.define_faixa(FaixaSonora::Lenta, 0);
    const Conta r = mede(&c, 0, 4999);
    EXPECT_EQ(r.bipes, 5U);
    EXPECT_EQ(r.ms_ligado, 5U * 100U);
}

TEST(CadenciaBuzzer, faixa_rapida_a_cada_350ms) {
    CadenciaBuzzer c;
    c.define_faixa(FaixaSonora::Rapida, 0);
    const Conta r = mede(&c, 0, 3499);
    EXPECT_EQ(r.bipes, 10U);
    EXPECT_EQ(r.ms_ligado, 10U * 100U);
}

TEST(CadenciaBuzzer, faixa_de_pulso_a_dez_hertz) {
    CadenciaBuzzer c;
    c.define_faixa(FaixaSonora::Pulso, 0);
    const Conta r = mede(&c, 0, 999);
    EXPECT_EQ(r.bipes, 10U);
    EXPECT_EQ(r.ms_ligado, 10U * 50U);
}

TEST(CadenciaBuzzer, liga_no_inicio_do_ciclo_e_desliga_no_fim_do_bipe) {
    CadenciaBuzzer c;
    c.define_faixa(FaixaSonora::Lenta, 0);
    EXPECT_TRUE(c.atualiza(0));
    EXPECT_TRUE(c.atualiza(99));
    EXPECT_FALSE(c.atualiza(100));   // fronteira: 100 ms ja e silencio
    EXPECT_FALSE(c.atualiza(999));
    EXPECT_TRUE(c.atualiza(1000));   // proximo ciclo
}

TEST(CadenciaBuzzer, a_fase_nao_depende_do_instante_em_que_se_pergunta) {
    // O laco nao roda em passos exatos de 1 ms. Perguntar so de 37 em 37 ms
    // tem de dar a mesma resposta que perguntar a cada 1 ms.
    CadenciaBuzzer fino;
    CadenciaBuzzer grosso;
    fino.define_faixa(FaixaSonora::Rapida, 0);
    grosso.define_faixa(FaixaSonora::Rapida, 0);
    for (std::uint32_t t = 0; t <= 3500; t += 37) {
        fino.atualiza(t);
        // o "fino" tambem passou por todos os instantes intermediarios
        for (std::uint32_t k = (t > 36 ? t - 36 : 0); k <= t; ++k) {
            grosso.atualiza(k);
        }
        EXPECT_EQ(fino.ligado(), grosso.ligado()) << "divergiu em t=" << t;
    }
}

// ============================================== idempotência: o erro fatal

TEST(CadenciaBuzzer, repetir_a_mesma_faixa_nao_reinicia_a_fase) {
    // O laco principal chama define_faixa() a CADA volta com o veredito do
    // Zonamento. Se repetir a mesma faixa reiniciasse a fase, o instante zero
    // seria sempre agora, o bipe nunca terminaria, e a faixa 3 viraria
    // exatamente o tom continuo que o RF03.7 proibe.
    CadenciaBuzzer c;
    Conta r;
    bool antes = false;
    for (std::uint32_t t = 0; t <= 999; ++t) {
        c.define_faixa(FaixaSonora::Pulso, t);   // a cada volta, como no main
        const bool agora = c.atualiza(t);
        if (agora) { ++r.ms_ligado; }
        if (agora && !antes) { ++r.bipes; }
        antes = agora;
    }
    EXPECT_EQ(r.bipes, 10U);
    EXPECT_EQ(r.ms_ligado, 500U) << "virou tom continuo";
}

TEST(CadenciaBuzzer, trocar_de_faixa_recomeca_o_padrao_de_imediato) {
    // Vindo de Lenta no meio do silencio, esperar o ciclo fechar custaria ate
    // 900 ms para o motorista ouvir que a situacao piorou.
    CadenciaBuzzer c;
    c.define_faixa(FaixaSonora::Lenta, 0);
    ASSERT_FALSE(c.atualiza(500));           // dentro do silencio do ciclo
    c.define_faixa(FaixaSonora::Pulso, 500);
    EXPECT_TRUE(c.ligado()) << "a nova faixa deveria apitar na hora";
    EXPECT_TRUE(c.atualiza(500));
}

TEST(CadenciaBuzzer, voltar_para_silencio_cala) {
    CadenciaBuzzer c;
    c.define_faixa(FaixaSonora::Pulso, 0);
    ASSERT_TRUE(c.atualiza(0));
    c.define_faixa(FaixaSonora::Nenhuma, 10);
    EXPECT_FALSE(c.atualiza(10));
    EXPECT_EQ(mede(&c, 10, 2000).ms_ligado, 0U);
}

TEST(CadenciaBuzzer, silencia_cala_sem_esperar_o_fim_do_bipe) {
    CadenciaBuzzer c;
    c.define_faixa(FaixaSonora::Lenta, 0);
    ASSERT_TRUE(c.atualiza(50));   // no meio do bipe
    c.silencia();
    EXPECT_FALSE(c.ligado());
    EXPECT_FALSE(c.atualiza(60));
    EXPECT_EQ(c.faixa(), FaixaSonora::Nenhuma);
}

// ============================================================ relógio longo

TEST(CadenciaBuzzer, a_fase_atravessa_o_estouro_do_relogio) {
    constexpr std::uint32_t kQuase = 0xFFFFFF00U;   // 256 ms antes do estouro
    CadenciaBuzzer c;
    c.define_faixa(FaixaSonora::Pulso, kQuase);
    Conta r;
    bool antes = false;
    for (std::uint32_t i = 0; i <= 999; ++i) {
        const bool agora = c.atualiza(static_cast<std::uint32_t>(kQuase + i));
        if (agora) { ++r.ms_ligado; }
        if (agora && !antes) { ++r.bipes; }
        antes = agora;
    }
    EXPECT_EQ(r.bipes, 10U) << "a cadencia tropecou no estouro de 49 dias";
    EXPECT_EQ(r.ms_ligado, 500U);
}

TEST(CadenciaBuzzer, um_salto_grande_de_relogio_nao_trava_ligado) {
    // Se o laco travar por segundos, a proxima volta nao pode encontrar o
    // buzzer preso: a fase e recalculada, nao arrastada.
    CadenciaBuzzer c;
    c.define_faixa(FaixaSonora::Lenta, 0);
    ASSERT_TRUE(c.atualiza(0));
    EXPECT_FALSE(c.atualiza(60'000 + 500));   // 60,5 s depois: meio do silencio
    EXPECT_TRUE(c.atualiza(61'000));          // e o ciclo seguinte apita
}

}  // namespace
