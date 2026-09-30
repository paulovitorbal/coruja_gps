#include "display/TelaMenu.h"

#include <gtest/gtest.h>

#include <string>
#include <cstdio>
#include <vector>

namespace {

using namespace coruja;

InfoAparelho info_exemplo() {
    InfoAparelho i;
    std::snprintf(i.nome, sizeof i.nome, "%s", "fusca");
    std::snprintf(i.versao_base, sizeof i.versao_base, "%s", "2026-09-15");
    i.pontos = 18304;
    i.taxa_hz = 3.6F;
    return i;
}
const InfoAparelho kInfo = info_exemplo();

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
    tela.desenha(b.menu(), kInfo, v);

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
    tela.desenha(b.menu(), kInfo, v);
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
    tela.desenha(b.menu(), kInfo, v);
    EXPECT_TRUE(v.tem_texto("escolher"));

    v.limpa();
    b.clica();                       // entra em edicao
    ASSERT_EQ(b.menu().estado(), EstadoMenu::Editando);
    tela.desenha(b.menu(), kInfo, v);
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
    tela.desenha(b.menu(), kInfo, v);
    const std::size_t antes = v.retangulos.size();

    v.limpa();
    b.clica();
    tela.desenha(b.menu(), kInfo, v);

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
    tela.desenha(b.menu(), kInfo, v);
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
    ASSERT_GT(tela.desenha(b.menu(), kInfo, v), 0);

    v.limpa();
    EXPECT_EQ(tela.desenha(b.menu(), kInfo, v), 0) << "redesenhou sem nada mudar";
    EXPECT_TRUE(v.textos.empty());

    v.limpa();
    b.gira(EventoEncoder::GiroDireita);
    EXPECT_GT(tela.desenha(b.menu(), kInfo, v), 0) << "nao redesenhou apos mudar";
}

TEST(TelaMenu, apresenta_e_chamado_quando_algo_muda) {
    // O Visor real nao tem framebuffer e ignora o apresenta(), mas a
    // interface o preve e um painel com buffer duplo dependeria dele.
    // Nao chamar seria um defeito que so aparece ao trocar de painel.
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    tela.desenha(b.menu(), kInfo, v);
    EXPECT_EQ(v.apresentacoes, 1U);

    v.limpa();
    tela.desenha(b.menu(), kInfo, v);
    EXPECT_EQ(v.apresentacoes, 0U) << "apresentou sem ter desenhado nada";

    v.limpa();
    b.gira(EventoEncoder::GiroDireita);
    tela.desenha(b.menu(), kInfo, v);
    EXPECT_EQ(v.apresentacoes, 1U);
}

TEST(TelaMenu, invalida_forca_o_redesenho_completo) {
    // Ao abrir o menu a tela por baixo era outra, entao o que o
    // instantaneo anterior diz nao vale mais.
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    tela.desenha(b.menu(), kInfo, v);
    v.limpa();
    tela.invalida();
    EXPECT_GT(tela.desenha(b.menu(), kInfo, v), 0);
    EXPECT_TRUE(v.tem_texto("AJUSTES"));
}

// --- a tela de informacao ---

TEST(TelaMenu, informacao_mostra_unidade_base_e_taxa) {
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    b.avanca_ate(ItemMenu::Informacao);
    b.clica();
    ASSERT_EQ(b.menu().estado(), EstadoMenu::Informando);
    tela.desenha(b.menu(), kInfo, v);

    EXPECT_TRUE(v.tem_texto("fusca")) << "nao diz QUAL unidade e";
    EXPECT_TRUE(v.tem_texto("2026-09-15"));
    EXPECT_TRUE(v.tem_texto("18304"));
    EXPECT_TRUE(v.tem_texto("3.6")) << "a taxa perdeu a casa decimal";
}

TEST(TelaMenu, o_nome_da_unidade_vem_primeiro) {
    // Com duas unidades, saber QUAL aparelho se esta olhando vale mais que
    // qualquer outro numero desta tela -- e e a razao de a chave `nome`
    // existir no coruja.cfg.
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    b.avanca_ate(ItemMenu::Informacao);
    b.clica();
    tela.desenha(b.menu(), kInfo, v);

    int y_nome = -1;
    int y_base = -1;
    for (const auto& t : v.textos) {
        if (t.s.find("fusca") != std::string::npos) { y_nome = t.y; }
        if (t.s.find("2026-09-15") != std::string::npos) { y_base = t.y; }
    }
    ASSERT_GE(y_nome, 0);
    ASSERT_GE(y_base, 0);
    EXPECT_LT(y_nome, y_base) << "o nome da unidade nao veio primeiro";
}

TEST(TelaMenu, a_taxa_do_gps_mantem_a_casa_decimal) {
    // 4,0 e 3,6 Hz e o que separa "normal" de "degradado" no RF01.5. Sem a
    // decimal os dois apareceriam como 4, e a tela mentiria.
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    InfoAparelho degradado = kInfo;
    degradado.taxa_hz = 3.6F;
    b.avanca_ate(ItemMenu::Informacao);
    b.clica();
    tela.desenha(b.menu(), degradado, v);
    EXPECT_TRUE(v.tem_texto("3.6"));
    EXPECT_FALSE(v.tem_texto("gps: 4")) << "arredondou e escondeu a queda";
}

TEST(TelaMenu, sem_nome_a_tela_diz_sem_nome_em_vez_de_vazio) {
    // Um cartao antigo nao tem a chave `nome`. Uma linha vazia pareceria
    // defeito de desenho; "sem nome" diz o que de fato acontece.
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    InfoAparelho anonimo;
    anonimo.pontos = 100;
    b.avanca_ate(ItemMenu::Informacao);
    b.clica();
    tela.desenha(b.menu(), anonimo, v);
    EXPECT_TRUE(v.tem_texto("sem nome"));
}

TEST(TelaMenu, sair_da_informacao_volta_a_navegar) {
    Bancada b;
    VisorEspiao v;
    TelaMenu tela;
    b.avanca_ate(ItemMenu::Informacao);
    b.clica();
    tela.desenha(b.menu(), kInfo, v);
    ASSERT_TRUE(v.tem_texto("18304"));

    v.limpa();
    b.gira(EventoEncoder::GiroDireita);
    ASSERT_EQ(b.menu().estado(), EstadoMenu::Navegando);
    tela.desenha(b.menu(), kInfo, v);
    EXPECT_FALSE(v.tem_texto("18304")) << "a informacao ficou na tela";
    EXPECT_TRUE(v.tem_texto("informacao")) << "voltou para o item errado";
}

}  // namespace
