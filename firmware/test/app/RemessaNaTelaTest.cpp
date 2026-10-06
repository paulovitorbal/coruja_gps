#include "app/RemessaNaTela.h"

#include <gtest/gtest.h>

#include <set>

namespace {

using namespace coruja;

constexpr FaseRemessa kTodas[] = {
    FaseRemessa::Conectando, FaseRemessa::Listando, FaseRemessa::Enviando,
    FaseRemessa::Confirmando, FaseRemessa::Apagando, FaseRemessa::Concluida,
    FaseRemessa::Falhou,
};

TEST(FaseEquivalente, os_desfechos_nao_se_confundem_com_as_fases) {
    // Concluída e Falhou governam a cor do LED e se a tela segura ou passa.
    // Mapear qualquer uma delas para uma fase intermediária deixaria o
    // aparelho preso numa tela que nunca sai.
    EXPECT_EQ(fase_equivalente(FaseRemessa::Concluida), FaseOta::Concluida);
    EXPECT_EQ(fase_equivalente(FaseRemessa::Falhou), FaseOta::Falhou);
    for (auto f : kTodas) {
        if (f == FaseRemessa::Concluida || f == FaseRemessa::Falhou) {
            continue;
        }
        EXPECT_NE(fase_equivalente(f), FaseOta::Concluida) << descreve(f);
        EXPECT_NE(fase_equivalente(f), FaseOta::Falhou) << descreve(f);
    }
}

TEST(FaseEquivalente, so_o_envio_cai_na_fase_com_barra) {
    // A barra só é desenhada na fase que a composição marca, e o progresso só
    // existe durante o envio. Qualquer outra fase caindo lá mostraria uma
    // barra parada em zero -- que diz "travou".
    for (auto f : kTodas) {
        const bool e_envio = f == FaseRemessa::Enviando;
        EXPECT_EQ(fase_equivalente(f) == FaseOta::Baixando, e_envio)
            << descreve(f);
    }
}

TEST(FaseEquivalente, toda_fase_tem_traducao_e_nenhuma_cai_no_padrao) {
    // O `return` final do switch é a rede de segurança para enum fora de
    // faixa; nenhuma fase REAL pode chegar nele, senão ela apareceria como
    // falha sem ter falhado.
    for (auto f : kTodas) {
        if (f == FaseRemessa::Falhou) { continue; }
        EXPECT_NE(fase_equivalente(f), FaseOta::Falhou) << descreve(f);
    }
}

TEST(FaseRemessaDescricao, toda_fase_tem_nome_proprio) {
    std::set<std::string> nomes;
    for (auto f : kTodas) {
        const std::string n = descreve(f);
        EXPECT_FALSE(n.empty());
        EXPECT_NE(n, "fase desconhecida");
        nomes.insert(n);
    }
    // Dois nomes iguais na tela são duas fases que o usuário não distingue.
    EXPECT_EQ(nomes.size(), std::size(kTodas));
}

}  // namespace
