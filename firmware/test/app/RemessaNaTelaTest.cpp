#include "app/RemessaNaTela.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

namespace {

using namespace coruja;

// Os mesmos dublês do `OtaNaTelaTest`: a `RemessaNaTela` desenha ATRAVÉS da
// `OtaNaTela`, e é pelo visor que dá para ver o que ela repassou.
class VisorEspiao : public Visor {
public:
    struct Ret { int x, y, l, a; Cor565 cor; };
    std::vector<Ret> retangulos;
    std::vector<std::string> textos;

    void retangulo(int x, int y, int l, int a, Cor565 c) override {
        retangulos.push_back({x, y, l, a, c});
    }
    void texto(int, int, const char* s, Fonte, Cor565, Alinhamento) override {
        textos.emplace_back(s);
    }
    void icone(int, int, Icone) override {}
    void apresenta() override {}

    void limpa() { retangulos.clear(); textos.clear(); }
    bool tem(const std::string& p) const {
        for (const auto& t : textos) {
            if (t.find(p) != std::string::npos) { return true; }
        }
        return false;
    }
};

class LedEspiao : public LedRgb {
public:
    void define_cor(const Cor& c) override { atual = c; }
    Cor cor_atual() const override { return atual; }
    Cor atual = cores::kApagado;
};

class PausaFalsa : public Pausa {
public:
    std::uint32_t agora = 0;
    void espera_ms(std::uint32_t ms) override { agora += ms; }
    std::uint32_t agora_ms() override { return agora; }
};

struct Bancada {
    VisorEspiao   visor;
    LedEspiao     led;
    PausaFalsa    relogio;
    RemessaNaTela tela{visor, led, relogio};
};

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

TEST(RemessaNaTela, a_faixa_mostra_o_arquivo_da_vez_e_nao_a_fase) {
    // Numa remessa de oito arquivos, "enviando" sozinho fica parado meio
    // minuto e nao diz se o aparelho anda. `3/8 coruja.log` diz.
    Bancada b;
    b.tela.fase(FaseRemessa::Enviando, "coruja.log", 3, 8, 1);
    EXPECT_TRUE(b.visor.tem("3/8 coruja.log"));
}

TEST(RemessaNaTela, uma_tentativa_nova_recomeca_a_barra_do_zero) {
    // A tentativa vai ADIANTE para a `OtaNaTela`, e nao fixa em 1 como
    // estava. Entre duas tentativas do MESMO arquivo a fase nao muda -- e
    // sem a tentativa, a `OtaNaTela` nao reconhece o aviso como novo: a
    // barra da segunda tentativa comecaria na porcentagem da primeira e
    // depois andaria PARA TRAS quando os bytes chegassem.
    //
    // Olha o ESTADO, e não o visor: a `TelaOta` só redesenha quando o
    // instantâneo muda, então com a tentativa perdida pelo caminho o segundo
    // aviso não pinta nada -- e o visor fica limpo pelo motivo errado. Uma
    // campanha de mutação mostrou isso: a versão pelo visor passava com a
    // tentativa fixada em 1.
    Bancada b;
    b.tela.fase(FaseRemessa::Enviando, "coruja.log", 1, 1, 1);
    b.tela.progresso(12'000, 20'000);
    ASSERT_EQ(b.tela.estado().recebidos, 12'000U);
    ASSERT_TRUE(b.visor.tem("60%"));

    b.tela.fase(FaseRemessa::Enviando, "coruja.log", 1, 1, 2);
    EXPECT_EQ(b.tela.estado().recebidos, 0U)
        << "a segunda tentativa herdou o progresso da primeira";
    EXPECT_EQ(b.tela.estado().total, 0U);
}

TEST(RemessaNaTela, sem_arquivo_a_faixa_mostra_a_fase) {
    Bancada b;
    b.tela.fase(FaseRemessa::Listando, "", 0, 0, 1);
    EXPECT_TRUE(b.visor.tem(descreve(FaseRemessa::Listando)));
}

}  // namespace
