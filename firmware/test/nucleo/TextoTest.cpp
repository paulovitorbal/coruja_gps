#include "nucleo/Texto.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace {

using namespace coruja;

std::string apara(const std::string& entrada) {
    char buffer[256] = {};
    std::memcpy(buffer, entrada.data(), entrada.size());
    std::size_t tamanho = entrada.size();
    apara_branco(buffer, &tamanho);
    EXPECT_EQ(std::strlen(buffer), tamanho);
    return std::string(buffer, tamanho);
}

TEST(Texto, TiraOFimDeLinhaDaRespostaDoServidor) {
    // O caso que motiva a função: sem isto, cada clique rebaixaria a mesma
    // base, porque "v1\n" nunca é igual a "v1".
    EXPECT_EQ(apara("crc32:d290d536 pontos:18294 formato:1\n"),
              "crc32:d290d536 pontos:18294 formato:1");
}

TEST(Texto, TiraCrLf) {
    EXPECT_EQ(apara("versao-7\r\n"), "versao-7");
}

TEST(Texto, TiraDasDuasPontas) {
    EXPECT_EQ(apara("  \t v1 \r\n "), "v1");
}

TEST(Texto, NaoMexeNoMeio) {
    // O espaço entre os campos da versão é significativo.
    EXPECT_EQ(apara("a b\tc\n"), "a b\tc");
}

TEST(Texto, TextoSoDeBrancoViraVazio) {
    EXPECT_EQ(apara(" \r\n\t "), "");
}

TEST(Texto, TextoVazioContinuaVazio) {
    EXPECT_EQ(apara(""), "");
}

TEST(Texto, TextoSemBrancoNaoMuda) {
    EXPECT_EQ(apara("v1"), "v1");
}

TEST(Texto, PonteirosNulosNaoEstouram) {
    std::size_t tamanho = 0;
    apara_branco(nullptr, &tamanho);
    char buffer[4] = "ab";
    apara_branco(buffer, nullptr);
    EXPECT_STREQ(buffer, "ab");
}

}  // namespace
