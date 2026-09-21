#include "nucleo/Url.h"

#include <gtest/gtest.h>

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

}  // namespace
