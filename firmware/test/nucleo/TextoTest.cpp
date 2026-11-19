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


// --- seguro_para_cabecalho ------------------------------------------------
//
// A guarda contra divisao de requisicao. O que entra aqui vem do coruja.cfg,
// que esta num cartao removivel e e relido a cada acao.

TEST(SeguroParaCabecalho, TextoComumPassa) {
    EXPECT_TRUE(seguro_para_cabecalho("https://coruja.exemplo.com/envio/"));
    EXPECT_TRUE(seguro_para_cabecalho("A1B2-c3d4_e5f6.g7"));
}

TEST(SeguroParaCabecalho, VazioPassa) {
    // "sem token" e um estado legitimo: quem decide o que fazer com ele nao
    // e esta funcao.
    EXPECT_TRUE(seguro_para_cabecalho(""));
}

TEST(SeguroParaCabecalho, NuloRecusa) {
    EXPECT_FALSE(seguro_para_cabecalho(nullptr));
}

TEST(SeguroParaCabecalho, RetornoDeCarroRecusa) {
    // O caso que da nome a coisa: isto viraria um cabecalho a mais na
    // requisicao que o aparelho manda.
    EXPECT_FALSE(seguro_para_cabecalho("abc\r\nX-Falso: 1"));
}

TEST(SeguroParaCabecalho, NovaLinhaSozinhaRecusa) {
    EXPECT_FALSE(seguro_para_cabecalho("abc\nX-Falso: 1"));
}

TEST(SeguroParaCabecalho, EspacoRecusa) {
    // Numa URL o espaco tem de vir percent-encoded; cru, ele parte a linha
    // de pedido `GET /caminho HTTP/1.1` em duas.
    EXPECT_FALSE(seguro_para_cabecalho("/a b"));
}

TEST(SeguroParaCabecalho, TabulacaoRecusa) {
    EXPECT_FALSE(seguro_para_cabecalho("/a\tb"));
}

TEST(SeguroParaCabecalho, NulDentroNaoExiste) {
    // Um \0 no meio termina o texto: o que vem depois nao e lido, e isso e
    // o comportamento certo -- a string C acaba ali.
    EXPECT_TRUE(seguro_para_cabecalho("ab"));
}

TEST(SeguroParaCabecalho, ExtremosDaFaixaPassam) {
    // 0x21 e 0x7E, as bordas exatas. Sem isto, um `<` ou um `~` recusado
    // passaria despercebido.
    const char baixo[] = {'!', '\0'};
    const char alto[]  = {'~', '\0'};
    EXPECT_TRUE(seguro_para_cabecalho(baixo));
    EXPECT_TRUE(seguro_para_cabecalho(alto));
}

TEST(SeguroParaCabecalho, LogoForaDaFaixaRecusa) {
    // 0x20 e 0x7F, os vizinhos imediatos das bordas.
    const char espaco[] = {' ', '\0'};
    const char del[]    = {'\x7f', '\0'};
    EXPECT_FALSE(seguro_para_cabecalho(espaco));
    EXPECT_FALSE(seguro_para_cabecalho(del));
}

TEST(SeguroParaCabecalho, ByteAltoRecusa) {
    // UTF-8 cru. `char` e com sinal no ARM e no host: a comparacao tem de
    // enxergar 0xC3 como 195, e nao como -61.
    const char acentuado[] = {'\xc3', '\xa1', '\0'};
    EXPECT_FALSE(seguro_para_cabecalho(acentuado));
}

// --- token_para_cabecalho --------------------------------------------------
//
// O token por chamada vem do `coruja.cfg` como todo o resto, e o cartao e
// removivel e relido a cada acao (RNF03). O `ClienteTls::define_token` ja
// recusava o token que o cliente guarda; o que ia por chamada passava
// direto -- e um `\r\n` nele vira cabecalho a mais na requisicao que o
// aparelho manda.

TEST(TokenParaCabecalho, o_da_chamada_vence_o_guardado) {
    EXPECT_STREQ(token_para_cabecalho("daremessa", "doboot"), "daremessa");
}

TEST(TokenParaCabecalho, sem_token_de_chamada_vale_o_guardado) {
    EXPECT_STREQ(token_para_cabecalho(nullptr, "doboot"), "doboot");
    EXPECT_STREQ(token_para_cabecalho("", "doboot"), "doboot");
}

TEST(TokenParaCabecalho, token_torto_nao_vai_e_nao_cai_no_guardado) {
    // Vazio, e nao o guardado: cair nele mascararia o erro do coruja.cfg
    // com o segredo certo, e o aparelho funcionaria com uma configuracao
    // quebrada. Sem token o servidor responde 401, que e visivel.
    EXPECT_STREQ(token_para_cabecalho("bom\r\nX-Outro: 1", "doboot"), "");
    EXPECT_STREQ(token_para_cabecalho("com espaco", "doboot"), "");
    EXPECT_STREQ(token_para_cabecalho("com\ttab", "doboot"), "");
}

TEST(TokenParaCabecalho, nunca_devolve_nulo) {
    // Vai direto para um `%s` do `snprintf` que monta a requisicao.
    EXPECT_NE(token_para_cabecalho(nullptr, nullptr), nullptr);
    EXPECT_STREQ(token_para_cabecalho(nullptr, nullptr), "");
}

}  // namespace
