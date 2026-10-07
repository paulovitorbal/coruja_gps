#include "rede/CaixaDns.h"

#include <gtest/gtest.h>

#include <set>

namespace {

using namespace coruja;

TEST(CaixaDns, resposta_do_pedido_em_curso_e_aceita) {
    CaixaDns caixa;
    const auto g = caixa.abre();
    EXPECT_FALSE(caixa.pronto());

    EXPECT_TRUE(caixa.entrega(g, true));
    EXPECT_TRUE(caixa.pronto());
    EXPECT_TRUE(caixa.achou());
}

TEST(CaixaDns, lwip_que_desistiu_marca_pronto_sem_achar) {
    // `dns_call_found(i, NULL)`: a resposta CHEGOU, e a notícia é que não há
    // endereço. Quem espera tem de sair do laço -- e não com um endereço
    // inventado.
    CaixaDns caixa;
    const auto g = caixa.abre();
    EXPECT_TRUE(caixa.entrega(g, false));
    EXPECT_TRUE(caixa.pronto());
    EXPECT_FALSE(caixa.achou());
}

TEST(CaixaDns, resposta_depois_de_abandonar_nao_e_aceita) {
    // O caso que dá nome à classe: o prazo estourou, quem esperava foi
    // embora, e o lwIP responde mais tarde. É o `false` devolvido aqui que
    // impede o chamador de copiar o endereço para uma variável que já morreu.
    CaixaDns caixa;
    const auto g = caixa.abre();
    caixa.abandona();
    EXPECT_FALSE(caixa.entrega(g, true));
    EXPECT_FALSE(caixa.pronto());
    EXPECT_FALSE(caixa.achou());
}

TEST(CaixaDns, resposta_atrasada_nao_contamina_o_pedido_seguinte) {
    // O pior arranjo possível: o NTP desiste em 5 s, o aparelho segue para o
    // HTTP, e só então o lwIP responde ao pedido do NTP. Se a geração antiga
    // fosse aceita, o pedido novo veria "pronto" por conta do antigo.
    CaixaDns caixa;
    const auto antiga = caixa.abre();
    caixa.abandona();
    const auto nova = caixa.abre();
    ASSERT_NE(antiga, nova);

    EXPECT_FALSE(caixa.entrega(antiga, true))
        << "a resposta do pedido abandonado entrou";
    EXPECT_FALSE(caixa.pronto());

    EXPECT_TRUE(caixa.entrega(nova, true));
    EXPECT_TRUE(caixa.pronto());
}

TEST(CaixaDns, resposta_atrasada_nao_entra_nem_com_o_pedido_novo_ja_resolvido) {
    // A ordem que o chamador depende: a caixa tem de recusar ANTES de ele
    // copiar o endereço. Sem isto, a resposta atrasada escreveria por cima do
    // endereço de um pedido novo que já havia resolvido -- e o aparelho
    // conectaria no host errado.
    CaixaDns caixa;
    const auto antiga = caixa.abre();
    const auto nova = caixa.abre();
    ASSERT_TRUE(caixa.entrega(nova, true));
    ASSERT_TRUE(caixa.pronto());

    EXPECT_FALSE(caixa.entrega(antiga, true));
}

TEST(CaixaDns, abrir_sem_abandonar_tambem_invalida_a_geracao_anterior) {
    // Nem todo caminho passa por `abandona`. Abrir já tem de bastar.
    CaixaDns caixa;
    const auto antiga = caixa.abre();
    caixa.abre();
    EXPECT_FALSE(caixa.entrega(antiga, true));
    EXPECT_FALSE(caixa.pronto());
}

TEST(CaixaDns, abrir_limpa_o_resultado_anterior) {
    CaixaDns caixa;
    const auto g = caixa.abre();
    caixa.entrega(g, true);
    ASSERT_TRUE(caixa.pronto());

    caixa.abre();
    EXPECT_FALSE(caixa.pronto());
    EXPECT_FALSE(caixa.achou());
}

TEST(CaixaDns, geracao_zero_nao_entra_com_pedido_em_curso) {
    CaixaDns caixa;
    caixa.abre();
    EXPECT_FALSE(caixa.entrega(0, true));
    EXPECT_FALSE(caixa.pronto());
}

TEST(CaixaDns, geracao_zero_nao_entra_DEPOIS_de_abandonar) {
    // É **aqui** que a guarda do zero importa, e o teste de cima não via:
    // com o pedido abandonado, `em_curso_` também vale zero, e sem a guarda
    // uma entrega de geração zero casaria com ele. Zero é "nenhum pedido em
    // curso" -- ele nunca pode ser o pedido em curso.
    //
    // A campanha de mutação encontrou esta cegueira: removendo `geracao == 0`
    // da condição, a suíte inteira continuava verde.
    CaixaDns caixa;
    caixa.abre();
    caixa.abandona();
    EXPECT_FALSE(caixa.entrega(0, true));
    EXPECT_FALSE(caixa.pronto());
    EXPECT_FALSE(caixa.achou());
}

TEST(CaixaDns, geracoes_consecutivas_nao_se_repetem) {
    // A propriedade da qual tudo depende, verificada e não suposta.
    CaixaDns caixa;
    std::set<std::uint32_t> vistas;
    for (int i = 0; i < 1000; ++i) {
        const auto g = caixa.abre();
        EXPECT_NE(g, 0u);
        EXPECT_TRUE(vistas.insert(g).second) << "geracao repetida: " << g;
        caixa.abandona();
    }
}


TEST(CaixaDns, a_volta_do_contador_nao_devolve_geracao_zero) {
    // Zero é "nenhum pedido em curso": uma geração zero faria TODA resposta
    // atrasada ser aceita depois de um `abandona`. A volta acontece depois de
    // 2^32 pedidos -- longe demais para um laço de teste, e perto o bastante
    // para o código ter de tratá-la. Uma campanha de mutação mostrou isto:
    // removendo o tratamento da volta, nada na suíte reclamava.
    CaixaDns caixa(0xFFFFFFFFU);
    const auto g = caixa.abre();
    EXPECT_NE(g, 0u);
    EXPECT_TRUE(caixa.entrega(g, true));
    EXPECT_TRUE(caixa.pronto());
}

TEST(CaixaDns, depois_da_volta_as_geracoes_seguem_avancando) {
    CaixaDns caixa(0xFFFFFFFFU);
    const auto primeira = caixa.abre();
    const auto segunda = caixa.abre();
    EXPECT_NE(primeira, 0u);
    EXPECT_NE(segunda, 0u);
    EXPECT_NE(primeira, segunda);
}

}  // namespace
