#include "nucleo/Url.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include <cstring>

namespace {

using namespace coruja;

Url analisa_ok(const char* texto) {
    Url u;
    EXPECT_EQ(analisa_url(texto, &u), ErroUrl::Nenhum) << texto;
    return u;
}

TEST(Url, HttpComPortaExplicita) {
    // A forma que o servidor de bancada usa de verdade.
    const Url u = analisa_ok("http://192.168.1.142:8080/radares.bin");
    EXPECT_STREQ(u.host, "192.168.1.142");
    EXPECT_EQ(u.porta, 8080);
    EXPECT_STREQ(u.caminho, "/radares.bin");
    EXPECT_FALSE(u.tls);
}

TEST(Url, HttpSemPortaAssume80) {
    const Url u = analisa_ok("http://exemplo.com/a/b.bin");
    EXPECT_STREQ(u.host, "exemplo.com");
    EXPECT_EQ(u.porta, 80);
    EXPECT_STREQ(u.caminho, "/a/b.bin");
}

TEST(Url, HttpsSemPortaAssume443ESinalizaTls) {
    const Url u = analisa_ok("https://exemplo.com/radares.versao");
    EXPECT_EQ(u.porta, 443);
    EXPECT_TRUE(u.tls);
}

TEST(Url, SemCaminhoViraBarra) {
    // Sem isso o GET sairia com URI vazia, que nenhum servidor aceita.
    const Url u = analisa_ok("http://exemplo.com");
    EXPECT_STREQ(u.host, "exemplo.com");
    EXPECT_STREQ(u.caminho, "/");
}

TEST(Url, AsDuasBarrasDoEsquemaNaoViramCaminho) {
    // O erro óbvio é procurar a primeira '/' do texto inteiro, que está em
    // "http://" — daria host vazio e caminho "//exemplo.com/x".
    const Url u = analisa_ok("http://exemplo.com/x");
    EXPECT_STREQ(u.host, "exemplo.com");
    EXPECT_STREQ(u.caminho, "/x");
}

TEST(Url, QueryStringFicaNoCaminho) {
    const Url u = analisa_ok("http://h:8080/v?forca=1&t=2");
    EXPECT_STREQ(u.caminho, "/v?forca=1&t=2");
    EXPECT_EQ(u.porta, 8080);
}

TEST(Url, EsquemaDesconhecidoERecusado) {
    Url u;
    EXPECT_EQ(analisa_url("ftp://exemplo/b.bin", &u), ErroUrl::EsquemaDesconhecido);
    EXPECT_EQ(analisa_url("exemplo/b.bin", &u), ErroUrl::EsquemaDesconhecido);
}

TEST(Url, VaziaOuNulaERecusada) {
    Url u;
    EXPECT_EQ(analisa_url("", &u), ErroUrl::Vazia);
    EXPECT_EQ(analisa_url(nullptr, &u), ErroUrl::Vazia);
    EXPECT_EQ(analisa_url("http://x/", nullptr), ErroUrl::Vazia);
}

TEST(Url, SemHostERecusada) {
    Url u;
    EXPECT_EQ(analisa_url("http:///caminho", &u), ErroUrl::SemHost);
    EXPECT_EQ(analisa_url("http://:8080/x", &u), ErroUrl::SemHost);
}

TEST(Url, PortaInvalidaERecusada) {
    Url u;
    EXPECT_EQ(analisa_url("http://h:/x", &u), ErroUrl::PortaInvalida);
    EXPECT_EQ(analisa_url("http://h:abc/x", &u), ErroUrl::PortaInvalida);
    EXPECT_EQ(analisa_url("http://h:0/x", &u), ErroUrl::PortaInvalida);
    EXPECT_EQ(analisa_url("http://h:70000/x", &u), ErroUrl::PortaInvalida);
    // 65535 é válido; 65536 não. A fronteira exata importa porque o campo é u16.
    EXPECT_EQ(analisa_url("http://h:65535/x", &u), ErroUrl::Nenhum);
    EXPECT_EQ(analisa_url("http://h:65536/x", &u), ErroUrl::PortaInvalida);
}

TEST(Url, IPv6LiteralERecusadoComMotivoProprio) {
    // Antes isto era ACEITO, com host "[::1]" e porta 8080 — o último ':' do
    // endereço passava por separador de porta. Falharia depois, no DNS, com
    // uma mensagem que não explica nada.
    Url u;
    EXPECT_EQ(analisa_url("http://[::1]:8080/x", &u), ErroUrl::Ipv6NaoSuportado);
    EXPECT_EQ(analisa_url("http://[fe80::1]/x", &u), ErroUrl::Ipv6NaoSuportado);
}

TEST(Url, UrlNoLimiteDaConfiguracaoCabe) {
    // kMaxUrl é o que o gera_config.py aceita; uma URL válida lá tem de caber
    // aqui, senão a configuração produz algo que o firmware recusa.
    std::string url = "http://exemplo.com/";
    url.append(kMaxUrl - url.size(), 'x');
    ASSERT_EQ(url.size(), kMaxUrl);
    Url u;
    EXPECT_EQ(analisa_url(url.c_str(), &u), ErroUrl::Nenhum);
}


// =========================================== o segredo do aparelho na URL

Url analisada(const char* texto) {
    Url u;
    EXPECT_EQ(analisa_url(texto, &u), ErroUrl::Nenhum) << texto;
    return u;
}

TEST(AcrescentaToken, poe_a_consulta_no_caminho) {
    Url u = analisada("http://servidor/radares.bin");
    ASSERT_TRUE(acrescenta_token(&u, "segredo"));
    EXPECT_STREQ(u.caminho, "/radares.bin?t=segredo");
    EXPECT_STREQ(u.host, "servidor") << "o host nao pode ser tocado";
    EXPECT_EQ(u.porta, 80);
}

TEST(AcrescentaToken, emenda_com_e_comercial_quando_ja_ha_consulta) {
    // Sem isto, uma url_base com parâmetro próprio viraria dois `?`, e o
    // servidor leria o segundo como parte do valor do primeiro.
    Url u = analisada("http://servidor/radares.bin?v=2");
    ASSERT_TRUE(acrescenta_token(&u, "segredo"));
    EXPECT_STREQ(u.caminho, "/radares.bin?v=2&t=segredo");
}

TEST(AcrescentaToken, caminho_raiz_tambem_funciona) {
    Url u = analisada("http://servidor");
    ASSERT_TRUE(acrescenta_token(&u, "segredo"));
    EXPECT_STREQ(u.caminho, "/?t=segredo");
}

TEST(AcrescentaToken, token_vazio_deixa_a_url_intacta) {
    // Um servidor sem lista de aparelhos não exige nada, e obrigar a
    // configurar um segredo para baixar a base quebraria quem só distribui.
    Url u = analisada("http://servidor/radares.bin");
    ASSERT_TRUE(acrescenta_token(&u, ""));
    EXPECT_STREQ(u.caminho, "/radares.bin");
    ASSERT_TRUE(acrescenta_token(&u, nullptr));
    EXPECT_STREQ(u.caminho, "/radares.bin");
}

TEST(AcrescentaToken, nao_trunca_quando_nao_cabe) {
    // Truncar faria o pedido ir para outro lugar, com o segredo cortado no
    // meio -- e o sintoma seria um 401 que não aponta para o tamanho.
    std::string longo = "http://servidor/";
    longo += std::string(kMaxUrl - 30, 'a');
    Url u = analisada(longo.c_str());
    const std::string antes = u.caminho;

    EXPECT_FALSE(acrescenta_token(&u, std::string(60, 'x').c_str()));
    EXPECT_EQ(std::string(u.caminho), antes) << "recusar nao pode sujar";
}

TEST(AcrescentaToken, no_limite_exato_ainda_cabe) {
    // O acréscimo custa `?t=` mais o token: 4 caracteres com um token de um.
    // O caminho que sobra para o resto é `kMaxUrl - 4`, e ele TEM de caber --
    // recusar aqui negaria uma URL perfeitamente válida.
    //
    // O tamanho é derivado da constante, não escrito à mão: na primeira
    // versão deste teste eu errei a conta por um, e o teste acusou o código
    // em vez da aritmética.
    constexpr std::size_t kCustoDoAcrescimo = 4;  // "?t=" + "x"
    const std::size_t caminho_cheio = kMaxUrl - kCustoDoAcrescimo;

    // O caminho já começa com a barra, então o preenchimento vem menos um.
    std::string base = "http://servidor/" + std::string(caminho_cheio - 1, 'a');
    Url u = analisada(base.c_str());
    ASSERT_EQ(std::strlen(u.caminho), caminho_cheio);

    EXPECT_TRUE(acrescenta_token(&u, "x"));
    EXPECT_EQ(std::strlen(u.caminho), kMaxUrl);
}

TEST(AcrescentaToken, um_caractere_alem_do_limite_e_recusado) {
    constexpr std::size_t kCustoDoAcrescimo = 4;
    const std::size_t grande_demais = kMaxUrl - kCustoDoAcrescimo + 1;
    std::string base = "http://servidor/" + std::string(grande_demais - 1, 'a');
    Url u = analisada(base.c_str());
    EXPECT_FALSE(acrescenta_token(&u, "x"));
}

TEST(TamanhoSemConsulta, corta_no_interrogacao) {
    // O segredo do aparelho viaja na consulta, e o log vai para o cartão --
    // que sai do carro. Mesma regra da senha de Wi-Fi.
    EXPECT_EQ(tamanho_sem_consulta("/radares.bin?t=segredo"), 12u);
    EXPECT_EQ(tamanho_sem_consulta("/radares.bin?v=2&t=segredo"), 12u);
    EXPECT_EQ(tamanho_sem_consulta("/?t=abc"), 1u);
}

TEST(TamanhoSemConsulta, caminho_sem_consulta_vai_inteiro) {
    EXPECT_EQ(tamanho_sem_consulta("/radares.bin"), 12u);
    EXPECT_EQ(tamanho_sem_consulta("/"), 1u);
    EXPECT_EQ(tamanho_sem_consulta(""), 0u);
    EXPECT_EQ(tamanho_sem_consulta(nullptr), 0u);
}

TEST(TamanhoSemConsulta, o_que_sobra_nao_contem_o_segredo) {
    // Afirma o EFEITO, e não a regra: o que importa é que a string que vai
    // ao log não carregue o valor, e não onde está o `?`.
    Url u = analisada("http://servidor/radares.bin");
    ASSERT_TRUE(acrescenta_token(&u, "nao-pode-vazar"));
    const std::string para_o_log(u.caminho,
                                 tamanho_sem_consulta(u.caminho));
    EXPECT_EQ(para_o_log, "/radares.bin");
    EXPECT_EQ(para_o_log.find("nao-pode-vazar"), std::string::npos);
}

TEST(AcrescentaToken, ponteiro_nulo_nao_quebra) {
    EXPECT_FALSE(acrescenta_token(nullptr, "segredo"));
}

TEST(AcrescentaToken, dois_acrescimos_nao_se_atropelam) {
    // Não é uso previsto, mas se acontecer o resultado tem de ser uma URL
    // válida e não um caminho corrompido.
    Url u = analisada("http://servidor/x");
    ASSERT_TRUE(acrescenta_token(&u, "a"));
    ASSERT_TRUE(acrescenta_token(&u, "b"));
    EXPECT_STREQ(u.caminho, "/x?t=a&t=b");
}

}  // namespace
