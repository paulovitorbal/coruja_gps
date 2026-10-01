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
    std::vector<Cor565> cores_de_texto;
    unsigned apresentacoes = 0;

    void retangulo(int x, int y, int l, int a, Cor565 c) override {
        retangulos.push_back({x, y, l, a, c});
    }
    void texto(int, int, const char* s, Fonte, Cor565 c, Alinhamento) override {
        textos.emplace_back(s);
        cores_de_texto.push_back(c);
    }
    void icone(int, int, Icone) override {}
    void apresenta() override { ++apresentacoes; }

    void limpa() {
        retangulos.clear(); textos.clear();
        cores_de_texto.clear(); apresentacoes = 0;
    }
    /// Cor com que `p` foi escrito, ou `kFundo` se nao foi escrito.
    Cor565 cor_do_texto(const std::string& p) const {
        for (std::size_t i = 0; i < textos.size(); ++i) {
            if (textos[i].find(p) != std::string::npos) {
                return cores_de_texto[i];
            }
        }
        return paleta::kFundo;
    }
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

// --- o motivo da falha ---
//
// "FALHOU" sozinho manda o usuario abrir o log para saber o que tentar, e
// quem esta com o aparelho na mao no carro nao vai abrir log nenhum. Cada
// falha pede uma acao diferente: senha errada, servidor fora, sinal
// instavel e cartao ruim nao se resolvem do mesmo jeito.

TEST(TelaOta, a_falha_mostra_o_motivo) {
    VisorEspiao v;
    TelaOta tela;
    EstadoOta e;
    e.fase = FaseOta::Falhou;
    e.motivo = "SEM WI-FI";
    tela.desenha(e, v);
    EXPECT_TRUE(v.tem("FALHOU"));
    EXPECT_TRUE(v.tem("SEM WI-FI"));
}

TEST(TelaOta, falha_sem_motivo_conhecido_ainda_diz_que_falhou) {
    // O `fase(Falhou)` parte de dentro do orquestrador, antes de alguem
    // saber o resultado. A tela nao pode ficar em branco nesse intervalo.
    VisorEspiao v;
    TelaOta tela;
    EstadoOta e;
    e.fase = FaseOta::Falhou;
    tela.desenha(e, v);
    EXPECT_TRUE(v.tem("FALHOU"));
}

TEST(TelaOta, o_motivo_so_aparece_na_falha) {
    // Um motivo sobrevivente de uma tentativa anterior apareceria sob
    // "ATUALIZADA", dizendo que deu certo e errado ao mesmo tempo.
    VisorEspiao v;
    TelaOta tela;
    EstadoOta e;
    e.fase = FaseOta::Concluida;
    e.motivo = "SEM WI-FI";
    tela.desenha(e, v);
    EXPECT_TRUE(v.tem("ATUALIZADA"));
    EXPECT_FALSE(v.tem("SEM WI-FI"));
}

TEST(TelaOta, trocar_o_motivo_repinta) {
    VisorEspiao v;
    TelaOta tela;
    EstadoOta e;
    e.fase = FaseOta::Falhou;
    e.motivo = "SEM WI-FI";
    ASSERT_GT(tela.desenha(e, v), 0);
    v.limpa();
    e.motivo = "CARTAO NAO GRAVOU";
    EXPECT_GT(tela.desenha(e, v), 0) << "o motivo mudou e a tela nao mexeu";
    EXPECT_TRUE(v.tem("CARTAO NAO GRAVOU"));
}

TEST(TelaOta, o_motivo_e_branco_e_a_palavra_FALHOU_nao) {
    // Paleta do 4.1: distinguir por MATIZ, nunca por luminancia, porque o
    // brilho do painel varia. O vermelho carrega "deu errado" e o branco
    // carrega a informacao, que e o que se le.
    VisorEspiao v;
    TelaOta tela;
    EstadoOta e;
    e.fase = FaseOta::Falhou;
    e.motivo = "SEM WI-FI";
    tela.desenha(e, v);
    EXPECT_EQ(v.cor_do_texto("FALHOU"), paleta::kBarraPerigo);
    EXPECT_EQ(v.cor_do_texto("SEM WI-FI"), paleta::kTexto);
}

// --- o passo de 5% ---
//
// O painel nao tem buffer duplo: cada redesenho APAGA a faixa e pinta por
// cima, e o olho pega esse intervalo. Pior, a faixa do meio carrega o rotulo
// "BAIXANDO", que nao muda durante o download e mesmo assim some e volta a
// cada repintura -- texto piscando incomoda muito mais que barra crescendo.
//
// Quantizar o percentual troca ~100 repinturas por 21. O aparelho continua
// sabendo o progresso exato; o que se arredonda e o que vai para a tela.

TEST(TelaOta, o_download_inteiro_cabe_em_vinte_e_uma_repinturas) {
    // O numero que importa: 0, 5, ... 100 sao 21 estados distintos. Alimenta
    // de 1 em 1 por cento, que e mais fino do que a rede jamais entrega.
    VisorEspiao v;
    TelaOta tela;
    int repinturas = 0;
    for (std::size_t i = 0; i <= 100; ++i) {
        if (tela.desenha(baixando(i, 100), v) > 0) { ++repinturas; }
    }
    EXPECT_EQ(repinturas, 21);
}

TEST(TelaOta, entre_dois_multiplos_de_cinco_a_tela_fica_parada) {
    // O coracao da mudanca. 45, 46, 47, 48 e 49 por cento desenham a MESMA
    // coisa, entao so o primeiro deles pode chegar ao painel.
    VisorEspiao v;
    TelaOta tela;
    ASSERT_GT(tela.desenha(baixando(45, 100), v), 0);
    for (std::size_t i = 46; i <= 49; ++i) {
        v.limpa();
        EXPECT_EQ(tela.desenha(baixando(i, 100), v), 0)
            << "repintou em " << i << "%";
    }
    v.limpa();
    EXPECT_GT(tela.desenha(baixando(50, 100), v), 0) << "nao repintou em 50%";
}

TEST(TelaOta, o_percentual_arredonda_para_BAIXO) {
    // Para baixo, nunca para o mais proximo: 49% virando 50% anunciaria
    // progresso que nao houve. Prometer a mais e o jeito de a barra parecer
    // travada no fim, que e justo quando o usuario pensa em desligar.
    VisorEspiao v;
    TelaOta tela;
    tela.desenha(baixando(49, 100), v);
    EXPECT_TRUE(v.tem("45%"));
    EXPECT_FALSE(v.tem("50%"));
}

TEST(TelaOta, o_fim_do_download_mostra_cem_por_cento) {
    // Arredondar para baixo nao pode comer o 100%: parar em 95% deixaria a
    // barra pela metade enquanto a fase seguinte ja comecou.
    VisorEspiao v;
    TelaOta tela;
    tela.desenha(baixando(100, 100), v);
    EXPECT_TRUE(v.tem("100%"));
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
