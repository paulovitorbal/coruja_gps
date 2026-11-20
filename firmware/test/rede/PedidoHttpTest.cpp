#include "rede/PedidoHttp.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace {

using namespace coruja;

Url url_de(const char* texto) {
    Url u;
    EXPECT_EQ(analisa_url(texto, &u), ErroUrl::Nenhum) << texto;
    return u;
}

/// O pedido inteiro como texto, ou vazio quando nao coube.
std::string pedido(const char* metodo, const char* texto_url,
                   const char* token = nullptr, std::size_t corpo = 0,
                   std::size_t capacidade = kMaxPedidoHttp) {
    char buffer[kMaxPedidoHttp + 64];
    EXPECT_LE(capacidade, sizeof buffer);
    const std::size_t n = monta_pedido_http(buffer, capacidade, metodo,
                                            url_de(texto_url), token, corpo);
    return n == 0 ? std::string() : std::string(buffer, n);
}

bool tem(const std::string& p, const std::string& trecho) {
    return p.find(trecho) != std::string::npos;
}

// ====================================================== a forma do pedido

TEST(PedidoHttp, a_linha_de_pedido_leva_metodo_caminho_e_versao) {
    const std::string p = pedido("GET", "https://exemplo.com/radares.bin");
    EXPECT_EQ(p.rfind("GET /radares.bin HTTP/1.1\r\n", 0), 0u) << p;
}

TEST(PedidoHttp, o_host_vai_sem_a_porta_padrao_e_termina_em_linha_branca) {
    const std::string p = pedido("GET", "https://exemplo.com/x");
    EXPECT_TRUE(tem(p, "Host: exemplo.com\r\n")) << p;
    EXPECT_EQ(p.substr(p.size() - 4), "\r\n\r\n") << "falta a linha em branco";
}

TEST(PedidoHttp, pede_conexao_fechada_e_corpo_sem_compressao) {
    // `Connection: close` porque o fim do fluxo marca o fim da resposta
    // quando o servidor nao diz tamanho. `identity` porque o Cloudflare
    // comprime por padrao, e um radares.bin em gzip chega com CRC que nao
    // bate -- este firmware nao descomprime.
    const std::string p = pedido("GET", "https://exemplo.com/x");
    EXPECT_TRUE(tem(p, "Connection: close\r\n")) << p;
    EXPECT_TRUE(tem(p, "Accept-Encoding: identity\r\n")) << p;
}

// ========================================================= Content-Length

TEST(PedidoHttp, sem_corpo_nao_vai_content_length) {
    // Havia um ternario com os DOIS RAMOS IGUAIS: a intencao de omitir
    // estava escrita e nunca acontecia, e todo GET saia com
    // `Content-Length: 0`.
    const std::string p = pedido("GET", "https://exemplo.com/x", nullptr, 0);
    EXPECT_FALSE(tem(p, "Content-Length")) << p;
}

TEST(PedidoHttp, com_corpo_vai_o_tamanho_exato) {
    const std::string p = pedido("PUT", "https://exemplo.com/envio/a.log",
                                 nullptr, 28'431);
    EXPECT_TRUE(tem(p, "Content-Length: 28431\r\n")) << p;
}

TEST(PedidoHttp, um_byte_de_corpo_ja_leva_content_length) {
    const std::string p = pedido("PUT", "https://exemplo.com/x", nullptr, 1);
    EXPECT_TRUE(tem(p, "Content-Length: 1\r\n")) << p;
}

// =================================================================== token

TEST(PedidoHttp, o_token_vai_no_cabecalho_e_nunca_na_url) {
    // No log de acesso do servidor e de qualquer proxy a consulta aparece;
    // o cabecalho, nao. Houve uma versao em que ele ia como `?t=`.
    const std::string p = pedido("GET", "https://exemplo.com/radares.bin",
                                 "segredo-do-carro");
    EXPECT_TRUE(tem(p, "X-Coruja-Token: segredo-do-carro\r\n")) << p;
    const std::string linha_de_pedido = p.substr(0, p.find("\r\n"));
    EXPECT_FALSE(tem(linha_de_pedido, "segredo-do-carro")) << linha_de_pedido;
}

TEST(PedidoHttp, sem_token_o_cabecalho_nao_aparece) {
    for (const char* t : {static_cast<const char*>(nullptr), ""}) {
        const std::string p = pedido("GET", "https://exemplo.com/x", t);
        EXPECT_FALSE(tem(p, "X-Coruja-Token")) << p;
    }
}

// ================================================== o que nao cabe nao sai

TEST(PedidoHttp, pedido_que_nao_cabe_devolve_zero) {
    // Zero, e nao um pedido truncado: cabecalho cortado vira requisicao que
    // o servidor le pela metade.
    EXPECT_TRUE(pedido("GET", "https://exemplo.com/x", nullptr, 0, 32).empty());
}

TEST(PedidoHttp, a_maior_url_com_o_maior_token_ainda_cabe) {
    // O par do teste acima, e o que fixa `kMaxPedidoHttp`: a configuracao
    // aceita `kMaxUrl` e `kMaxToken`, entao o pior caso que o aparelho pode
    // montar tem de caber.
    const std::string host(60, 'h');
    const std::string caminho(kMaxUrl - host.size() - 9, 'c');
    char texto[kMaxUrl + 16];
    std::snprintf(texto, sizeof texto, "https://%s/%s", host.c_str(),
                  caminho.c_str());
    const std::string token(kMaxToken, 't');

    const std::string p = pedido("PUT", texto, token.c_str(), 999'999);
    EXPECT_FALSE(p.empty()) << "o pior caso do aparelho nao coube";
    EXPECT_TRUE(tem(p, token)) << "o token saiu truncado";
}

// ============================================ qual token vai na requisicao
//
// O token por chamada vem do `coruja.cfg` como todo o resto, e o cartao e
// removivel e relido a cada acao (RNF03). O `ClienteTls::define_token` ja
// recusava o token que o cliente guarda; o que ia por chamada passava
// direto -- e um `\r\n` nele vira cabecalho a mais na requisicao que o
// aparelho manda, montado por quem escreveu no cartao.

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
    // O resultado vai direto para o `monta_pedido_http`.
    EXPECT_NE(token_para_cabecalho(nullptr, nullptr), nullptr);
    EXPECT_STREQ(token_para_cabecalho(nullptr, nullptr), "");
}

TEST(TokenParaCabecalho, o_token_torto_nao_chega_a_requisicao) {
    // As duas pecas juntas, que e como o cliente as usa.
    const std::string p = pedido("PUT", "https://exemplo.com/envio/a.log",
                                 token_para_cabecalho("x\r\nX-Outro: 1", ""),
                                 4);
    EXPECT_FALSE(tem(p, "X-Outro")) << p;
    EXPECT_FALSE(tem(p, "X-Coruja-Token")) << p;
}

TEST(PedidoHttp, destino_nulo_ou_capacidade_zero_devolve_zero) {
    const Url u = url_de("https://exemplo.com/x");
    char buffer[64];
    EXPECT_EQ(monta_pedido_http(nullptr, sizeof buffer, "GET", u, nullptr, 0),
              0u);
    EXPECT_EQ(monta_pedido_http(buffer, 0, "GET", u, nullptr, 0), 0u);
    EXPECT_EQ(monta_pedido_http(buffer, sizeof buffer, nullptr, u, nullptr, 0),
              0u);
}

}  // namespace
