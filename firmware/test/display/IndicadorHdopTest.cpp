#include "display/TelaPrincipal.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using namespace coruja;

/// Só o que este arquivo precisa ver: os retângulos desenhados.
class VisorDeBarras : public Visor {
public:
    struct Ret { int x, y, l, a; Cor565 cor; };
    std::vector<Ret> retangulos;
    std::vector<std::pair<int, std::string>> textos;

    void retangulo(int x, int y, int l, int a, Cor565 c) override {
        retangulos.push_back({x, y, l, a, c});
    }
    void texto(int x, int, const char* s, Fonte, Cor565,
               Alinhamento) override {
        textos.push_back({x, s});
    }
    void icone(int, int, Icone) override {}
    void apresenta() override {}

    /// Os retângulos que caem DENTRO da faixa superior e não são o
    /// apagamento dela — ou seja, as barras.
    std::vector<Ret> barras() const {
        std::vector<Ret> saida;
        for (const auto& r : retangulos) {
            const bool na_faixa = r.y >= tela::kYFaixaSuperior &&
                                  r.y < tela::kYFaixaSuperior +
                                            tela::kFaixaSuperior;
            if (na_faixa && r.l < tela::kLargura) { saida.push_back(r); }
        }
        return saida;
    }
};

EstadoTela dirigindo(FaixaHdop hdop) {
    EstadoTela e;
    e.tem_fix = true;
    e.telemetria.velocidade_kmh = 60.0F;
    e.telemetria.data_valida = true;
    e.telemetria.ano = 2026; e.telemetria.mes = 10; e.telemetria.dia = 13;
    e.telemetria.hora = 14; e.telemetria.minuto = 35;
    e.veredito.zona = Zona::Segura;
    e.hdop = hdop;
    return e;
}

TEST(IndicadorHdop, ceu_bom_acende_todas_as_barras) {
    VisorDeBarras v;
    TelaPrincipal tela;
    tela.desenha(dirigindo(FaixaHdop::Ideal), 0, v);
    EXPECT_EQ(static_cast<int>(v.barras().size()), kBarrasHdop);
}

TEST(IndicadorHdop, ceu_ruim_desenha_o_trilho_mas_nao_acende) {
    // O trilho continua lá, com as barras apagadas: um indicador que SOME
    // quando o sinal piora não distingue "ruim" de "não há indicador", e é
    // justamente quando piora que ele precisa ser visto.
    VisorDeBarras v;
    TelaPrincipal tela;
    tela.desenha(dirigindo(FaixaHdop::Ruim), 0, v);
    const auto bs = v.barras();
    EXPECT_EQ(static_cast<int>(bs.size()), kBarrasHdop);
    for (const auto& b : bs) {
        EXPECT_EQ(b.cor, paleta::kMoldura) << "barra acesa com HDOP ruim";
    }
}

TEST(IndicadorHdop, sem_medida_nao_desenha_indicador_nenhum) {
    // Sem GGA válida não há o que afirmar. Mostrar o trilho vazio diria
    // "geometria péssima", que é outra coisa.
    VisorDeBarras v;
    TelaPrincipal tela;
    tela.desenha(dirigindo(FaixaHdop::Desconhecida), 0, v);
    EXPECT_TRUE(v.barras().empty());
}

TEST(IndicadorHdop, quanto_melhor_a_geometria_mais_barras_acesas) {
    const FaixaHdop ordem[] = {FaixaHdop::Ruim, FaixaHdop::Moderado,
                               FaixaHdop::Bom, FaixaHdop::Excelente,
                               FaixaHdop::Ideal};
    int anterior = -1;
    for (const auto f : ordem) {
        VisorDeBarras v;
        TelaPrincipal tela;
        tela.desenha(dirigindo(f), 0, v);
        int acesas = 0;
        for (const auto& b : v.barras()) {
            if (b.cor != paleta::kMoldura) { ++acesas; }
        }
        EXPECT_GT(acesas, anterior) << "faixa " << descreve(f);
        anterior = acesas;
    }
}

TEST(IndicadorHdop, as_barras_ficam_DENTRO_da_faixa_superior) {
    VisorDeBarras v;
    TelaPrincipal tela;
    tela.desenha(dirigindo(FaixaHdop::Ideal), 0, v);
    for (const auto& b : v.barras()) {
        EXPECT_GE(b.y, tela::kYFaixaSuperior);
        EXPECT_LE(b.y + b.a,
                  tela::kYFaixaSuperior + tela::kFaixaSuperior);
        EXPECT_LE(b.x + b.l, tela::kLargura);
        EXPECT_GE(b.x, 0);
    }
}

TEST(IndicadorHdop, o_texto_da_faixa_nao_passa_por_baixo_das_barras) {
    // O relógio é centralizado na faixa. Sem encurtar a área dele, ele
    // cresceria para a direita e os dois se sobreporiam -- e o que se perde
    // num sobreposto é a legibilidade dos dois.
    VisorDeBarras v;
    TelaPrincipal tela;
    tela.desenha(dirigindo(FaixaHdop::Ideal), 0, v);
    int menor_x_barra = tela::kLargura;
    for (const auto& b : v.barras()) {
        if (b.x < menor_x_barra) { menor_x_barra = b.x; }
    }
    ASSERT_LT(menor_x_barra, tela::kLargura);
    ASSERT_FALSE(v.textos.empty());
    for (const auto& [x, s] : v.textos) {
        // O FIM do texto, e nao o comeco: um relogio que comeca a esquerda
        // das barras ainda pode terminar por baixo delas, e e o fim que
        // decide a sobreposicao.
        EXPECT_LE(x + largura_da_fonte(Fonte::TextoGrande, s.c_str()),
                  menor_x_barra)
            << "texto '" << s << "' invade as barras";
    }
}

TEST(IndicadorHdop, mudar_de_faixa_redesenha) {
    // As faixas da tela só redesenham quando o conteúdo muda. Se o
    // indicador não entrar nessa comparação, ele congela na primeira
    // leitura e passa a mentir.
    VisorDeBarras v;
    TelaPrincipal tela;
    tela.desenha(dirigindo(FaixaHdop::Ideal), 0, v);
    v.retangulos.clear();
    v.textos.clear();
    const int regioes = tela.desenha(dirigindo(FaixaHdop::Ruim), 1000, v);
    EXPECT_GT(regioes, 0);
    EXPECT_FALSE(v.barras().empty());
}

TEST(IndicadorHdop, faixa_igual_nao_redesenha_a_toa) {
    VisorDeBarras v;
    TelaPrincipal tela;
    tela.desenha(dirigindo(FaixaHdop::Bom), 0, v);
    v.retangulos.clear();
    const int regioes = tela.desenha(dirigindo(FaixaHdop::Bom), 0, v);
    EXPECT_EQ(regioes, 0);
}

}  // namespace
