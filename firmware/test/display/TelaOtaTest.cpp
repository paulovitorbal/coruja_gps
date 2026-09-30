#include "display/TelaOta.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using namespace coruja;

class VisorEspiao : public Visor {
public:
    struct Ret { int x, y, l, a; Cor565 cor; };
    std::vector<Ret> retangulos;
    std::vector<std::string> textos;
    unsigned apresentacoes = 0;

    void retangulo(int x, int y, int l, int a, Cor565 c) override {
        retangulos.push_back({x, y, l, a, c});
    }
    void texto(int, int, const char* s, Fonte, Cor565, Alinhamento) override {
        textos.emplace_back(s);
    }
    void icone(int, int, Icone) override {}
    void apresenta() override { ++apresentacoes; }

    void limpa() { retangulos.clear(); textos.clear(); apresentacoes = 0; }
    bool tem(const std::string& p) const {
        for (const auto& t : textos) {
            if (t.find(p) != std::string::npos) { return true; }
        }
        return false;
    }
    bool tem_cor(Cor565 c) const {
        for (const auto& r : retangulos) { if (r.cor == c) { return true; } }
        return false;
    }
    /// Largura preenchida da barra, ou -1 se ela nao foi desenhada.
    int barra_cheia() const {
        for (const auto& r : retangulos) {
            if (r.cor == paleta::kBarraAmbar) { return r.l; }
        }
        return -1;
    }
};

EstadoOta baixando(std::size_t recebidos, std::size_t total,
                   unsigned tentativa = 1) {
    EstadoOta e;
    e.fase = FaseOta::Baixando;
    e.tentativa = tentativa;
    e.recebidos = recebidos;
    e.total = total;
    return e;
}

TEST(TelaOta, mostra_o_nome_da_fase) {
    // Cada fase falha por motivo diferente: sem rede e senha ou alcance,
    // falha ao consultar e servidor fora, base recusada e arquivo
    // corrompido. Um "erro" genérico obrigaria a abrir o log.
    VisorEspiao v;
    TelaOta tela;
    EstadoOta e;
    e.fase = FaseOta::Conectando;
    tela.desenha(e, v);
    EXPECT_TRUE(v.tem("ATUALIZANDO")) << "a faixa superior nao diz o contexto";
    EXPECT_TRUE(v.tem("CONECTANDO"));
}

TEST(TelaOta, sem_total_conhecido_nao_desenha_barra) {
    // Antes do cabecalho da base nao ha denominador, e uma barra que enche
    // sozinha sem referencia mentiria sobre o andamento.
    VisorEspiao v;
    TelaOta tela;
    tela.desenha(baixando(5000, 0), v);
    EXPECT_EQ(v.barra_cheia(), -1) << "desenhou barra sem saber o total";
    EXPECT_FALSE(v.tem("%"));
}

TEST(TelaOta, com_total_a_barra_e_proporcional) {
    VisorEspiao v;
    TelaOta tela;
    tela.desenha(baixando(50, 200), v);
    EXPECT_TRUE(v.tem("25%"));
    const int quarto = v.barra_cheia();
    ASSERT_GT(quarto, 0);

    v.limpa();
    tela.desenha(baixando(150, 200), v);
    EXPECT_TRUE(v.tem("75%"));
    EXPECT_GT(v.barra_cheia(), quarto * 2) << "a barra nao acompanhou";
}

TEST(TelaOta, a_barra_nao_passa_de_cem_por_cento) {
    // O verificador conta o corpo inteiro; um servidor que mande mais do
    // que o cabecalho promete nao pode estourar a barra na tela.
    VisorEspiao v;
    TelaOta tela;
    tela.desenha(baixando(300, 200), v);
    EXPECT_TRUE(v.tem("100%"));
}

TEST(TelaOta, a_tentativa_aparece_quando_e_repeticao) {
    // "BAIXANDO 2/3" diz que algo deu errado e esta sendo refeito. Sem
    // isso, uma retentativa parece travamento -- e travamento e o que faz
    // o usuario desligar o aparelho no meio da gravacao.
    VisorEspiao v;
    TelaOta tela;
    tela.desenha(baixando(10, 200, 2), v);
    EXPECT_TRUE(v.tem("2/3"));
}

TEST(TelaOta, a_primeira_tentativa_nao_mostra_contador) {
    // Mostrar "1/3" na primeira vez sugeriria que algo ja falhou.
    VisorEspiao v;
    TelaOta tela;
    tela.desenha(baixando(10, 200, 1), v);
    EXPECT_FALSE(v.tem("1/3"));
}

TEST(TelaOta, a_barra_de_progresso_e_ambar_e_nao_verde) {
    // Verde diria "pronto", e a barra fala do que esta em curso. O verde
    // fica para o fim, no LED.
    VisorEspiao v;
    TelaOta tela;
    tela.desenha(baixando(100, 200), v);
    EXPECT_TRUE(v.tem_cor(paleta::kBarraAmbar));
}

TEST(TelaOta, ja_em_dia_e_diferente_de_atualizada) {
    // Quem clicou esperava uma atualizacao. Dizer "atualizada" quando nada
    // foi baixado e mentira pequena com consequencia: o usuario acharia
    // que a base mudou.
    VisorEspiao v;
    TelaOta tela;
    EstadoOta e;
    e.fase = FaseOta::JaEmDia;
    tela.desenha(e, v);
    EXPECT_TRUE(v.tem("EM DIA"));
    EXPECT_FALSE(v.tem("ATUALIZADA"));
}

TEST(TelaOta, so_redesenha_o_que_mudou) {
    VisorEspiao v;
    TelaOta tela;
    const auto e = baixando(50, 200);
    ASSERT_GT(tela.desenha(e, v), 0);
    v.limpa();
    EXPECT_EQ(tela.desenha(e, v), 0) << "redesenhou sem nada mudar";
}

TEST(TelaOta, invalida_forca_o_redesenho) {
    // Ao entrar no OTA a tela por baixo era outra.
    VisorEspiao v;
    TelaOta tela;
    const auto e = baixando(50, 200);
    tela.desenha(e, v);
    v.limpa();
    tela.invalida();
    EXPECT_GT(tela.desenha(e, v), 0);
}

}  // namespace
