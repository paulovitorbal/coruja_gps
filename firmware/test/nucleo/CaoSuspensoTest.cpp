#include "nucleo/CaoSuspenso.h"

#include <gtest/gtest.h>

#include <stdexcept>

#include "apoio/CaoDeGuardaFalso.h"

namespace {

using namespace coruja;

TEST(CaoSuspenso, solta_ao_nascer_e_devolve_ao_morrer) {
    teste::CaoDeGuardaFalso cao;
    {
        CaoSuspenso guarda(&cao);
        EXPECT_EQ(cao.suspensoes, 1U);
        EXPECT_EQ(cao.retomadas, 0U) << "devolveu o cao antes da hora";
    }
    EXPECT_EQ(cao.retomadas, 1U) << "o cao ficou solto depois do escopo";
}

/// A saida antecipada e o caso que o par manual esquece.
unsigned funcao_com_retorno_no_meio(teste::CaoDeGuardaFalso& cao, bool sai) {
    CaoSuspenso guarda(&cao);
    if (sai) { return 1; }
    return 2;
}

TEST(CaoSuspenso, a_saida_antecipada_retoma) {
    teste::CaoDeGuardaFalso cao;
    EXPECT_EQ(funcao_com_retorno_no_meio(cao, true), 1U);
    EXPECT_EQ(cao.retomadas, 1U) << "um return no meio deixou o cao solto";
    EXPECT_EQ(funcao_com_retorno_no_meio(cao, false), 2U);
    EXPECT_EQ(cao.retomadas, 2U);
}

TEST(CaoSuspenso, sem_cao_nao_faz_nada) {
    CaoSuspenso guarda(nullptr);
    SUCCEED();
}

}  // namespace
