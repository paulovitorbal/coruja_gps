#include "display/TelaMenu.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using namespace coruja;

class VisorEspiao : public Visor {
public:
    struct Ret { int x, y, l, a; Cor565 cor; };
    struct Txt { int x, y; std::string s; Fonte f; Cor565 cor;
                 Alinhamento alin; };
    std::vector<Ret> retangulos;
    std::vector<Txt> textos;
    unsigned apresentacoes = 0;

    void retangulo(int x, int y, int l, int a, Cor565 c) override {
        retangulos.push_back({x, y, l, a, c});
    }
    void texto(int x, int y, const char* s, Fonte f, Cor565 c,
               Alinhamento a) override {
        textos.push_back({x, y, s, f, c, a});
    }
    void icone(int, int, Icone) override {}
    void apresenta() override { ++apresentacoes; }

    void limpa() { retangulos.clear(); textos.clear(); apresentacoes = 0; }

    bool tem_texto(const std::string& parte) const {
        for (const auto& t : textos) {
            if (t.s.find(parte) != std::string::npos) { return true; }
        }
        return false;
    }
    const Txt* em_fonte(Fonte f) const {
        for (const auto& t : textos) {
            if (t.f == f) { return &t; }
        }
        return nullptr;
    }
};

/// Menu aberto, pronto para o teste dizer o que fazer.
class Bancada {
public:
    Bancada() : menu_(Configuracao{}) { gira(EventoEncoder::GiroDireita); }

    void gira(EventoEncoder e) {
        agora_ += 100;
        menu_.avalia(e, /*parado=*/true, agora_);
    }
    void clica() { gira(EventoEncoder::Clique); }
    void avanca_ate(ItemMenu alvo) {
        for (int i = 0; i < 20 && menu_.item() != alvo; ++i) {
            gira(EventoEncoder::GiroDireita);
        }
    }
    MenuAjustes& menu() { return menu_; }

private:
    MenuAjustes menu_;
    std::uint32_t agora_ = 1000;
};

TEST(TelaMenu, mostra_o_rotulo_e_o_valor_do_item_corrente) {
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    tela.desenha(b.menu(), v);

    EXPECT_TRUE(v.tem_texto("AJUSTES")) << "a faixa superior diz onde se esta";
    EXPECT_TRUE(v.tem_texto("brilho"));
    EXPECT_TRUE(v.tem_texto("%")) << "o valor do brilho sumiu";
}

TEST(TelaMenu, o_valor_e_maior_que_o_rotulo) {
    // A hierarquia e a mesma da tela de dirigir: o que muda ao girar fica
    // grande. Sem isso o olho teria de procurar o que mudou.
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    tela.desenha(b.menu(), v);
    const auto* valor = v.em_fonte(Fonte::NumeroPequeno);
    ASSERT_NE(valor, nullptr) << "o valor nao foi desenhado em fonte propria";
    EXPECT_GT(altura_da_fonte(Fonte::NumeroPequeno),
              altura_da_fonte(Fonte::Texto));
}

TEST(TelaMenu, o_rodape_diz_o_que_o_encoder_faz_agora) {
    // E a unica pista de que girar deixou de navegar e passou a editar.
    // Sem ela, o usuario descobriria a diferenca mexendo -- o que num menu
    // de brilho e volume significa mexer no que nao queria.
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    tela.desenha(b.menu(), v);
    EXPECT_TRUE(v.tem_texto("escolher"));

    v.limpa();
    b.clica();                       // entra em edicao
    ASSERT_EQ(b.menu().estado(), EstadoMenu::Editando);
    tela.desenha(b.menu(), v);
    EXPECT_TRUE(v.tem_texto("mudar"));
    EXPECT_TRUE(v.tem_texto("confirmar"));
}

TEST(TelaMenu, edicao_e_marcada_por_forma_e_nao_por_cor) {
    // Regra 1 da paleta: distinguir por matiz ou forma, NUNCA por
    // luminancia -- o PWM do backlight multiplica a luminancia de tudo
    // pelo mesmo fator, e no piso de brilho dois brancos diferentes viram
    // o mesmo branco. Por isso a marca de edicao e um sublinhado.
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    tela.desenha(b.menu(), v);
    const std::size_t antes = v.retangulos.size();

    v.limpa();
    b.clica();
    tela.desenha(b.menu(), v);

    bool tem_sublinhado = false;
    for (const auto& r : v.retangulos) {
        if (r.cor == paleta::kTexto && r.a <= 6 && r.l > 10) {
            tem_sublinhado = true;
        }
    }
    EXPECT_TRUE(tem_sublinhado) << "nada marca o modo de edicao";
    EXPECT_GT(v.retangulos.size() + antes, antes);

    // E o texto continua branco nos dois modos: a marca e adicional, nao
    // substitutiva.
    for (const auto& t : v.textos) {
        EXPECT_EQ(t.cor, paleta::kTexto);
    }
}

TEST(TelaMenu, itens_de_acao_nao_mostram_valor) {
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    b.avanca_ate(ItemMenu::AtualizarBase);
    tela.desenha(b.menu(), v);
    EXPECT_TRUE(v.tem_texto("atualizar"));
    EXPECT_EQ(v.em_fonte(Fonte::NumeroPequeno), nullptr)
        << "acao nao tem valor a mostrar";
}

TEST(TelaMenu, so_redesenha_o_que_mudou) {
    // Mesma razao da TelaPrincipal: redesenhar a tela inteira a cada volta
    // custa SPI que o laco do alerta nao tem para gastar.
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    ASSERT_GT(tela.desenha(b.menu(), v), 0);

    v.limpa();
    EXPECT_EQ(tela.desenha(b.menu(), v), 0) << "redesenhou sem nada mudar";
    EXPECT_TRUE(v.textos.empty());

    v.limpa();
    b.gira(EventoEncoder::GiroDireita);
    EXPECT_GT(tela.desenha(b.menu(), v), 0) << "nao redesenhou apos mudar";
}

TEST(TelaMenu, apresenta_e_chamado_quando_algo_muda) {
    // O Visor real nao tem framebuffer e ignora o apresenta(), mas a
    // interface o preve e um painel com buffer duplo dependeria dele.
    // Nao chamar seria um defeito que so aparece ao trocar de painel.
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    tela.desenha(b.menu(), v);
    EXPECT_EQ(v.apresentacoes, 1U);

    v.limpa();
    tela.desenha(b.menu(), v);
    EXPECT_EQ(v.apresentacoes, 0U) << "apresentou sem ter desenhado nada";

    v.limpa();
    b.gira(EventoEncoder::GiroDireita);
    tela.desenha(b.menu(), v);
    EXPECT_EQ(v.apresentacoes, 1U);
}

TEST(TelaMenu, invalida_forca_o_redesenho_completo) {
    // Ao abrir o menu a tela por baixo era outra, entao o que o
    // instantaneo anterior diz nao vale mais.
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    tela.desenha(b.menu(), v);
    v.limpa();
    tela.invalida();
    EXPECT_GT(tela.desenha(b.menu(), v), 0);
    EXPECT_TRUE(v.tem_texto("AJUSTES"));
}

}  // namespace
