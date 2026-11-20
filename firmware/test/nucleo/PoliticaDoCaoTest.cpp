#include "nucleo/PoliticaDoCao.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

/// O registrador: guarda a palavra, e e ele que "sobrevive ao reset" -- cada
/// boot do teste cria uma `PoliticaDoCao` nova sobre o MESMO armazem.
class ArmazemFalso : public ArmazemDePalavra {
public:
    std::uint32_t valor = 0;
    std::uint32_t le() const override { return valor; }
    void grava(std::uint32_t v) override { valor = v; }
};

PoliticaDoCao::Decisao boot(ArmazemFalso& a, bool mordida) {
    PoliticaDoCao p(a);
    return p.decide_no_boot(mordida);
}

TEST(PoliticaDoCao, religamento_normal_arma_e_nao_conta_mordida) {
    ArmazemFalso a;
    const auto d = boot(a, false);
    EXPECT_TRUE(d.arma);
    EXPECT_EQ(d.mordidas_seguidas, 0U);
}

TEST(PoliticaDoCao, cada_mordida_seguida_sobe_o_contador) {
    ArmazemFalso a;
    EXPECT_EQ(boot(a, true).mordidas_seguidas, 1U);
    EXPECT_EQ(boot(a, true).mordidas_seguidas, 2U);
}

TEST(PoliticaDoCao, ate_a_segunda_mordida_o_cao_continua_armado) {
    ArmazemFalso a;
    EXPECT_TRUE(boot(a, true).arma);
    EXPECT_TRUE(boot(a, true).arma);
}

TEST(PoliticaDoCao, na_terceira_mordida_seguida_nao_arma) {
    ArmazemFalso a;
    boot(a, true);
    boot(a, true);
    const auto d = boot(a, true);
    EXPECT_EQ(d.mordidas_seguidas, PoliticaDoCao::kMordidasSeguidasMax);
    EXPECT_FALSE(d.arma) << "o ciclo de reinicios nao foi quebrado";
}

TEST(PoliticaDoCao, o_boot_desarmado_nao_zera_por_saude_e_o_seguinte_rearma) {
    ArmazemFalso a;
    boot(a, true);
    boot(a, true);
    PoliticaDoCao p(a);
    ASSERT_FALSE(p.decide_no_boot(true).arma);
    // Rodou horas desarmado: sem cao nao ha saude a provar.
    p.passo(0);
    p.passo(10u * PoliticaDoCao::kSaudavelMs);
    EXPECT_EQ(a.valor & 0xFFFFu, 3U);
    // A proxima ignicao nao e mordida (nao havia cao): zera e rearma.
    const auto d = boot(a, false);
    EXPECT_TRUE(d.arma);
    EXPECT_EQ(d.mordidas_seguidas, 0U);
}

TEST(PoliticaDoCao, religamento_normal_zera_a_contagem) {
    ArmazemFalso a;
    boot(a, true);
    boot(a, true);
    EXPECT_EQ(boot(a, false).mordidas_seguidas, 0U);
    // E a mordida seguinte conta do um, nao do tres.
    const auto d = boot(a, true);
    EXPECT_EQ(d.mordidas_seguidas, 1U);
    EXPECT_TRUE(d.arma);
}

TEST(PoliticaDoCao, rodar_saudavel_o_tempo_todo_zera_a_contagem) {
    ArmazemFalso a;
    boot(a, true);
    PoliticaDoCao p(a);
    ASSERT_EQ(p.decide_no_boot(true).mordidas_seguidas, 2U);
    p.passo(5'000);
    p.passo(5'000 + PoliticaDoCao::kSaudavelMs - 1);
    EXPECT_EQ(a.valor & 0xFFFFu, 2U) << "zerou antes do tempo";
    p.passo(5'000 + PoliticaDoCao::kSaudavelMs);
    EXPECT_EQ(a.valor & 0xFFFFu, 0U) << "nao zerou depois do tempo";
    // A mordida seguinte e uma mordida isolada.
    EXPECT_EQ(boot(a, true).mordidas_seguidas, 1U);
}

TEST(PoliticaDoCao, o_tempo_saudavel_conta_desde_o_primeiro_passo_e_nao_desde_zero) {
    // O relogio ja esta longe da origem quando o laco comeca.
    ArmazemFalso a;
    boot(a, true);
    PoliticaDoCao p(a);
    p.decide_no_boot(true);
    p.passo(3'000'000);
    p.passo(3'000'000 + PoliticaDoCao::kSaudavelMs - 1);
    EXPECT_NE(a.valor & 0xFFFFu, 0U);
}

TEST(PoliticaDoCao, a_virada_do_relogio_nao_zera_antes_da_hora) {
    ArmazemFalso a;
    PoliticaDoCao p(a);
    p.decide_no_boot(true);
    p.passo(0xFFFFFFFFu - 1000u);
    p.passo(1000u);   // passaram 2 s, nao 49 dias
    EXPECT_EQ(a.valor & 0xFFFFu, 1U);
}

TEST(PoliticaDoCao, lixo_no_registrador_e_ignorado) {
    for (const std::uint32_t lixo : {0xDEADBEEFu, 0xFFFFFFFFu, 0x00000007u,
                                     0xC0A60002u}) {
        ArmazemFalso a;
        a.valor = lixo;
        const auto d = boot(a, true);
        EXPECT_EQ(d.mordidas_seguidas, 1U) << std::hex << lixo;
        EXPECT_TRUE(d.arma);
    }
}

TEST(PoliticaDoCao, lixo_num_religamento_e_limpo_e_nao_vira_contagem) {
    ArmazemFalso a;
    a.valor = 0xC0A7FFFFu & 0x0000FFFFu;   // sem marca
    boot(a, false);
    EXPECT_EQ(a.valor, PoliticaDoCao::kMarca);
}

TEST(PoliticaDoCao, a_contagem_satura_e_nao_da_a_volta) {
    ArmazemFalso a;
    a.valor = PoliticaDoCao::kMarca | 0xFFFFu;
    const auto d = boot(a, true);
    EXPECT_FALSE(d.arma) << "a volta do contador rearmaria o cao";
}

}  // namespace
