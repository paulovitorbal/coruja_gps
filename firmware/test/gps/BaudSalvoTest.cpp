// A lembrança do baud do módulo.
//
// ⚠️ **Dado estragado aqui abre a porta na velocidade errada**, que é a falha
// de 08/10/2026 por outro caminho: o aparelho lê um fluxo ilegível e fica
// mudo com o receptor funcionando do outro lado. Por isso a leitura recusa
// tudo que não seja exatamente um dos dois bauds conhecidos.

#include "gps/BaudSalvo.h"

#include <gtest/gtest.h>

#include <cstring>

namespace {

using namespace coruja;

std::uint32_t le(const char* texto, bool* ok = nullptr) {
    std::uint32_t v = 0;
    const bool r = analisa_baud(texto, std::strlen(texto), &v);
    if (ok != nullptr) { *ok = r; }
    return r ? v : 0;
}

TEST(BaudSalvo, grava_e_le_de_volta) {
    char buf[kTamBaudSalvo] = {};
    const auto n = formata_baud(115200, buf, sizeof buf);
    ASSERT_GT(n, 0u);
    std::uint32_t v = 0;
    ASSERT_TRUE(analisa_baud(buf, n, &v));
    EXPECT_EQ(v, 115200u);
}

TEST(BaudSalvo, grava_e_le_o_baud_de_fabrica) {
    char buf[kTamBaudSalvo] = {};
    const auto n = formata_baud(9600, buf, sizeof buf);
    ASSERT_GT(n, 0u);
    std::uint32_t v = 0;
    ASSERT_TRUE(analisa_baud(buf, n, &v));
    EXPECT_EQ(v, 9600u);
}

TEST(BaudSalvo, e_legivel_a_olho_no_cartao) {
    // O arquivo é diagnóstico também: quem abre o cartão num computador tem
    // de entender o que está lendo sem consultar o código.
    char buf[kTamBaudSalvo] = {};
    formata_baud(115200, buf, sizeof buf);
    EXPECT_STREQ(buf, "baud=115200\n");
}

TEST(BaudSalvo, baud_desconhecido_nao_e_gravado) {
    // Gravar o que a leitura vai recusar deixaria no cartão um lixo com cara
    // de configuração.
    char buf[kTamBaudSalvo] = {};
    EXPECT_EQ(formata_baud(57600, buf, sizeof buf), 0u);
    EXPECT_EQ(formata_baud(0, buf, sizeof buf), 0u);
}

TEST(BaudSalvo, buffer_curto_nao_estoura) {
    char buf[4] = {};
    EXPECT_EQ(formata_baud(115200, buf, sizeof buf), 0u);
}

TEST(BaudSalvo, baud_fora_dos_dois_conhecidos_e_recusado) {
    bool ok = true;
    le("baud=57600\n", &ok);
    EXPECT_FALSE(ok);
}

TEST(BaudSalvo, lixo_e_recusado) {
    for (const char* texto : {"", "baud=", "baud=abc\n", "nada=115200\n",
                              "115200\n", "baud=11x5200\n", " baud=9600\n"}) {
        bool ok = true;
        le(texto, &ok);
        EXPECT_FALSE(ok) << texto;
    }
}

TEST(BaudSalvo, numero_absurdamente_longo_nao_transborda) {
    // Sem o teto de dígitos, um arquivo cheio de algarismos daria a volta no
    // `uint32` e poderia cair em cima de um baud válido por acidente.
    bool ok = true;
    le("baud=00000000000000115200\n", &ok);
    EXPECT_FALSE(ok);
}

TEST(BaudSalvo, espaco_depois_do_numero_e_tolerado) {
    bool ok = false;
    EXPECT_EQ(le("baud=9600  \n", &ok), 9600u);
    EXPECT_TRUE(ok);
}

TEST(BaudSalvo, sem_quebra_de_linha_tambem_vale) {
    bool ok = false;
    EXPECT_EQ(le("baud=115200", &ok), 115200u);
    EXPECT_TRUE(ok);
}

TEST(BaudSalvo, destino_nulo_nao_quebra) {
    EXPECT_FALSE(analisa_baud("baud=9600\n", 10, nullptr));
    EXPECT_EQ(formata_baud(9600, nullptr, 32), 0u);
}

}  // namespace
