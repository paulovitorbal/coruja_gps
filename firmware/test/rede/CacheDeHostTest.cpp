#include "rede/CacheDeHost.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace coruja;

TEST(CacheDeHost, vazio_nao_serve_ninguem) {
    CacheDeHost c;
    EXPECT_FALSE(c.serve("servidor.exemplo", 0));
}

TEST(CacheDeHost, serve_o_mesmo_nome_dentro_do_prazo) {
    CacheDeHost c;
    c.guarda("servidor.exemplo", 1'000);
    EXPECT_TRUE(c.serve("servidor.exemplo", 1'000));
    EXPECT_TRUE(c.serve("servidor.exemplo", 1'000 + kValidadeDoHostMs - 1));
}

TEST(CacheDeHost, nao_serve_no_instante_em_que_o_prazo_vence) {
    // A borda é fechada do lado de fora: no instante exato o endereço já não
    // vale. Um `<=` aqui faria o prazo durar um milissegundo a mais, o que
    // não muda nada em campo -- mas a borda precisa estar escrita e testada,
    // senão ninguém sabe qual das duas o código faz.
    CacheDeHost c;
    c.guarda("servidor.exemplo", 1'000);
    EXPECT_FALSE(c.serve("servidor.exemplo", 1'000 + kValidadeDoHostMs));
}

TEST(CacheDeHost, outro_nome_nao_aproveita_o_endereco_guardado) {
    // O erro que isto impede é o pior possível: o pedido sairia para o
    // endereço de OUTRO servidor, e o aparelho mandaria o log para lá.
    CacheDeHost c;
    c.guarda("servidor.exemplo", 0);
    EXPECT_FALSE(c.serve("outro.exemplo", 0));
}

TEST(CacheDeHost, um_nome_que_e_prefixo_do_outro_nao_casa) {
    CacheDeHost c;
    c.guarda("coruja.exemplo", 0);
    EXPECT_FALSE(c.serve("coruja.exemplo.br", 0));
    c.guarda("coruja.exemplo.br", 0);
    EXPECT_FALSE(c.serve("coruja.exemplo", 0));
}

TEST(CacheDeHost, esquecer_derruba_o_que_estava_guardado) {
    CacheDeHost c;
    c.guarda("servidor.exemplo", 0);
    ASSERT_TRUE(c.serve("servidor.exemplo", 0));
    c.esquece();
    EXPECT_FALSE(c.serve("servidor.exemplo", 0));
}

TEST(CacheDeHost, guardar_de_novo_renova_o_prazo) {
    CacheDeHost c;
    c.guarda("servidor.exemplo", 0);
    c.guarda("servidor.exemplo", kValidadeDoHostMs);
    EXPECT_TRUE(c.serve("servidor.exemplo", kValidadeDoHostMs * 2 - 1));
}

TEST(CacheDeHost, atravessa_a_virada_do_relogio_de_32_bits) {
    // O `to_ms_since_boot` dá a volta a cada ~49 dias. Com subtração com
    // sinal, tudo guardado antes da virada passaria a parecer velho de 49
    // dias, e o cache deixaria de servir até o aparelho ser desligado.
    const std::uint32_t antes = 0xFFFFFFFFu - 1'000;
    CacheDeHost c;
    c.guarda("servidor.exemplo", antes);
    // 2 000 ms depois, já do outro lado da virada.
    EXPECT_TRUE(c.serve("servidor.exemplo", antes + 2'000));
    EXPECT_FALSE(c.serve("servidor.exemplo", antes + kValidadeDoHostMs));
}

TEST(CacheDeHost, nome_vazio_ou_nulo_nao_serve_e_nao_guarda) {
    CacheDeHost c;
    c.guarda("servidor.exemplo", 0);
    EXPECT_FALSE(c.serve(nullptr, 0));
    EXPECT_FALSE(c.serve("", 0));

    c.guarda("", 0);
    EXPECT_FALSE(c.serve("servidor.exemplo", 0))
        << "guardar lixo tinha de limpar o que estava la";
    c.guarda("servidor.exemplo", 0);
    c.guarda(nullptr, 0);
    EXPECT_FALSE(c.serve("servidor.exemplo", 0));
}

TEST(CacheDeHost, nome_grande_demais_nao_e_guardado_truncado) {
    // Truncar faria dois nomes com o mesmo começo se confundirem -- e aí o
    // pedido sai para o endereço do outro, que é o erro que o teste do
    // prefixo descreve, só que sem ninguém poder perceber.
    CacheDeHost c;
    const std::string grande(kMaxUrl + 1, 'a');
    c.guarda(grande.c_str(), 0);
    EXPECT_FALSE(c.serve(grande.c_str(), 0));
    EXPECT_FALSE(c.serve(std::string(kMaxUrl, 'a').c_str(), 0));
}

TEST(CacheDeHost, o_nome_do_tamanho_maximo_ainda_cabe) {
    CacheDeHost c;
    const std::string limite(kMaxUrl, 'a');
    c.guarda(limite.c_str(), 0);
    EXPECT_TRUE(c.serve(limite.c_str(), 0));
}

TEST(CacheDeHost, a_validade_e_configuravel_para_o_teste_e_para_o_futuro) {
    CacheDeHost c(10);
    c.guarda("servidor.exemplo", 100);
    EXPECT_TRUE(c.serve("servidor.exemplo", 109));
    EXPECT_FALSE(c.serve("servidor.exemplo", 110));
}

}  // namespace
