#include "log/CarimboExecucao.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace coruja;

const char* fonte_fixa() { return "2026-10-07T14:41:22Z"; }
const char* fonte_vazia() { return ""; }
const char* fonte_nula() { return nullptr; }

struct CarimboNoHost : public ::testing::Test {
    void SetUp() override { esquece_fonte_de_carimbo(); }
    void TearDown() override { esquece_fonte_de_carimbo(); }
};

TEST_F(CarimboNoHost, sem_fonte_o_carimbo_e_vazio_e_nao_ha_separador) {
    // O estado do host e o das linhas gravadas antes de o boot instalar a
    // fonte. Linha comecando com espaco solto e sujeira que ninguem conserta
    // depois porque ninguem sabe de onde veio.
    EXPECT_STREQ(carimbo_agora(), "");
    EXPECT_STREQ(separador_carimbo(), "");
}

TEST_F(CarimboNoHost, com_fonte_o_carimbo_sai_e_o_separador_aparece) {
    define_fonte_de_carimbo(fonte_fixa);
    EXPECT_STREQ(carimbo_agora(), "2026-10-07T14:41:22Z");
    EXPECT_STREQ(separador_carimbo(), " ");
}

TEST_F(CarimboNoHost, fonte_que_devolve_nulo_nao_estoura) {
    // Um `const char*` nulo chegando ao `%s` do snprintf e comportamento
    // indefinido -- e a fonte e codigo de plataforma, fora do alcance desta
    // suite. A guarda e aqui.
    define_fonte_de_carimbo(fonte_nula);
    EXPECT_STREQ(carimbo_agora(), "");
    EXPECT_STREQ(separador_carimbo(), "");
}

TEST_F(CarimboNoHost, fonte_que_devolve_vazio_nao_poe_separador) {
    define_fonte_de_carimbo(fonte_vazia);
    EXPECT_STREQ(separador_carimbo(), "");
}

TEST_F(CarimboNoHost, desinstalar_volta_ao_estado_de_antes_do_boot) {
    define_fonte_de_carimbo(fonte_fixa);
    ASSERT_STREQ(separador_carimbo(), " ");
    esquece_fonte_de_carimbo();
    EXPECT_STREQ(carimbo_agora(), "");
}

TEST_F(CarimboNoHost, nullptr_desliga_o_carimbo) {
    define_fonte_de_carimbo(fonte_fixa);
    define_fonte_de_carimbo(nullptr);
    EXPECT_STREQ(carimbo_agora(), "");
}

}  // namespace
