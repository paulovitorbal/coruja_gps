#include "rede/PrazoDeProgresso.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

constexpr std::uint32_t kInatividade = 15'000;
constexpr std::uint32_t kTeto = 180'000;

PrazoDeProgresso faz(std::uint32_t agora = 0) {
    return PrazoDeProgresso(kInatividade, kTeto, agora);
}

TEST(PrazoDeProgresso, nao_expira_enquanto_a_janela_de_silencio_nao_fecha) {
    auto p = faz();
    EXPECT_FALSE(p.expirou(kInatividade - 1));
}

TEST(PrazoDeProgresso, expira_com_a_janela_de_silencio_cheia) {
    auto p = faz();
    EXPECT_TRUE(p.expirou(kInatividade));
}

TEST(PrazoDeProgresso, progresso_adia_a_desistencia) {
    // O caso de 09/10/2026: enlace a -88 dBm, lento mas vivo. Antes disto o
    // pedido morria no meio do corpo com bytes subindo.
    auto p = faz();
    for (std::uint32_t t = 10'000; t <= 100'000; t += 10'000) {
        ASSERT_FALSE(p.expirou(t)) << "desistiu em t=" << t;
        p.registra_progresso(t);
    }
}

TEST(PrazoDeProgresso, um_enlace_que_rasteja_ainda_encontra_o_teto) {
    // Progresso constante, mas longe de terminar: sem o teto o aparelho
    // ficaria preso para sempre neste envio.
    auto p = faz();
    for (std::uint32_t t = 1'000; t < kTeto; t += 1'000) {
        p.registra_progresso(t);
    }
    EXPECT_TRUE(p.expirou(kTeto));
}

TEST(PrazoDeProgresso, o_teto_vale_mesmo_com_progresso_no_ultimo_instante) {
    auto p = faz();
    p.registra_progresso(kTeto);
    EXPECT_TRUE(p.expirou(kTeto));
}

TEST(PrazoDeProgresso, distingue_parado_de_teto_estourado) {
    // Duas causas, dois lugares para procurar: parado manda olhar o sinal,
    // teto manda olhar o tamanho do arquivo.
    auto parado = faz();
    EXPECT_TRUE(parado.expirou(kInatividade));
    EXPECT_FALSE(parado.estourou_o_teto(kInatividade));

    auto rastejando = faz();
    for (std::uint32_t t = 1'000; t <= kTeto; t += 1'000) {
        rastejando.registra_progresso(t);
    }
    EXPECT_TRUE(rastejando.estourou_o_teto(kTeto));
}

TEST(PrazoDeProgresso, atravessa_a_virada_do_relogio_de_32_bits) {
    // ~49 dias de aparelho ligado. Comparação com sinal daria negativo e o
    // pedido seria abortado na hora.
    // `0 - 5000` e cinco segundos ANTES da virada. `0xFFFFFFFF - 5000` seria
    // 5001, porque passar de 0xFFFFFFFF para 0 ja conta um milissegundo.
    const std::uint32_t quase_fim = 0U - 5'000U;
    PrazoDeProgresso p(kInatividade, kTeto, quase_fim);
    const std::uint32_t depois_da_virada = 4'000U;   // 9 s de verdade
    EXPECT_EQ(p.decorrido(depois_da_virada), 9'000U);
    EXPECT_FALSE(p.expirou(depois_da_virada));
}

TEST(PrazoDeProgresso, o_progresso_tambem_atravessa_a_virada) {
    const std::uint32_t quase_fim = 0U - 5'000U;
    PrazoDeProgresso p(kInatividade, kTeto, quase_fim);
    p.registra_progresso(2'000U);                 // já depois da virada
    EXPECT_EQ(p.parado_ha(10'000U), 8'000U);
    EXPECT_FALSE(p.expirou(10'000U));
}

TEST(PrazoDeProgresso, teto_e_inatividade_sao_independentes) {
    // Teto curto, inatividade longa: quem manda e o teto.
    PrazoDeProgresso p(100'000, 5'000, 0);
    EXPECT_TRUE(p.expirou(5'000));
}

}  // namespace
