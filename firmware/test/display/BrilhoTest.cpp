#include "display/Brilho.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

TEST(Brilho, comeca_no_maximo) {
    Brilho b;
    EXPECT_EQ(b.percentual(), 100);
}

TEST(Brilho, o_piso_e_5_por_cento_e_nunca_zero) {
    // Com o visor apagado o usuario perde a referencia visual para
    // recupera-lo: nao ha como achar o encoder no escuro sem feedback.
    Brilho b;
    for (int i = 0; i < 50; ++i) { b.diminui(); }
    EXPECT_EQ(b.percentual(), kBrilhoMinimoPct);
    EXPECT_GT(b.duty(), 0) << "chegou a apagar de verdade";
}

TEST(Brilho, o_teto_e_100_e_nao_passa) {
    Brilho b;
    for (int i = 0; i < 50; ++i) { b.aumenta(); }
    EXPECT_EQ(b.percentual(), kBrilhoMaximoPct);
}

TEST(Brilho, cada_clique_move_cinco_por_cento) {
    Brilho b;
    b.diminui();
    EXPECT_EQ(b.percentual(), 95);
    b.diminui();
    EXPECT_EQ(b.percentual(), 90);
    b.aumenta();
    EXPECT_EQ(b.percentual(), 95);
}

TEST(Brilho, sao_vinte_passos_do_piso_ao_teto) {
    Brilho b;
    for (int i = 0; i < 100; ++i) { b.diminui(); }
    int cliques = 0;
    while (b.percentual() < kBrilhoMaximoPct) { b.aumenta(); ++cliques; }
    EXPECT_EQ(cliques, static_cast<int>(kPassosBrilho) - 1);
}

TEST(Brilho, o_duty_cresce_sempre) {
    Brilho b;
    for (int i = 0; i < 100; ++i) { b.diminui(); }
    std::uint16_t anterior = 0;
    for (std::size_t i = 0; i < kPassosBrilho; ++i) {
        EXPECT_GT(b.duty(), anterior) << "no passo " << i;
        anterior = b.duty();
        b.aumenta();
    }
}

TEST(Brilho, a_curva_e_perceptual_e_nao_linear) {
    // ESTE e o ponto do requisito. Com passos lineares de duty, toda a
    // mudanca perceptivel acontece no fundo da escala e os dez cliques de
    // cima nao fazem nada. Na curva de 2,2, metade do brilho PERCEBIDO sao
    // ~22% do duty -- bem longe dos 50% que o linear daria.
    // Sobe ate 50% em vez de contar passos a partir do piso: contando, o
    // teste quebra toda vez que o piso mudar -- e ele ja mudou uma vez,
    // de 5% para 10% (R-05). O que se afirma aqui e sobre o MEIO da
    // escala, e nao tem por que depender de onde ela comeca.
    Brilho b;
    for (int i = 0; i < 100; ++i) { b.diminui(); }
    while (b.percentual() < 50) { b.aumenta(); }
    ASSERT_EQ(b.percentual(), 50);
    const double fracao = b.duty() / 65535.0;
    EXPECT_LT(fracao, 0.30) << "a curva achatou e virou quase linear";
    EXPECT_GT(fracao, 0.15) << "a curva exagerou e o meio da escala sumiu";
}

TEST(Brilho, o_piso_e_visivel_mas_discreto) {
    // O piso subiu de 5% para 10% no R-05, medido no painel: a 5% a barra
    // inferior nao se enxergava. Em duty isso e 413 de 65535, ~0,63% --
    // ainda muito abaixo de 1%, que e o que permite dirigir a noite sem
    // ofuscamento, e agora acima do limiar de visibilidade medido.
    Brilho b;
    for (int i = 0; i < 100; ++i) { b.diminui(); }
    EXPECT_EQ(b.percentual(), kBrilhoMinimoPct);
    EXPECT_GT(b.duty(), 0);
    EXPECT_LT(b.duty(), 655) << "o piso nao esta baixo o bastante para a noite";
}

TEST(Brilho, o_piso_fisico_do_R05_nao_muda_sem_medir_de_novo) {
    // Guarda de requisito, nao de implementacao. O 10% saiu de medicao no
    // painel real (R-05); muda-lo exige repetir a medicao, e este teste
    // existe para que isso seja decisao consciente e nao consequencia de
    // alguem "arredondando" a constante.
    //
    // Repare em QUAL constante isto guarda: `kPisoFisicoPct`, nao
    // `kBrilhoMinimoPct`. A primeira e a grandeza medida; a segunda e o
    // zero da escala do usuario, que e convencao e nao medicao. Guardar a
    // errada foi o que este teste fez por uma versao.
    EXPECT_EQ(kPisoFisicoPct, 10)
        << "piso fisico alterado: refaca o R-05 na bancada antes disto";
}

TEST(Brilho, a_escala_do_usuario_vai_de_zero_a_cem) {
    // O piso do hardware nao vaza para a interface: quem le 0% nao precisa
    // saber que o painel esta em 10% de luminancia.
    EXPECT_EQ(kBrilhoMinimoPct, 0);
    EXPECT_EQ(kBrilhoMaximoPct, 100);
    EXPECT_EQ(kPassosBrilho, 21u) << "0 a 100 de 5 em 5 sao 21 passos";

    Brilho b;
    for (int i = 0; i < 100; ++i) { b.diminui(); }
    EXPECT_EQ(b.percentual(), 0) << "o fundo da escala tem de se chamar 0%";
    for (int i = 0; i < 100; ++i) { b.aumenta(); }
    EXPECT_EQ(b.percentual(), 100);
}

TEST(Brilho, zero_por_cento_nao_apaga_a_tela) {
    // O 0% da escala e o mais escuro UTILIZAVEL, nao o apagado. Quem apaga
    // e o duty zero, que a curva nunca produz -- e com o visor escuro o
    // usuario perderia a referencia para recupera-lo.
    Brilho b;
    for (int i = 0; i < 100; ++i) { b.diminui(); }
    ASSERT_EQ(b.percentual(), 0);
    EXPECT_GT(b.duty(), 0) << "0% na escala nao pode significar tela apagada";
}

// ================================================ dia e noite (R-61)

TEST(Brilho, a_noite_comeca_mais_escura_que_o_dia) {
    Brilho b;
    const std::uint8_t dia = b.percentual();
    b.define_periodo(PeriodoDoDia::Noite);
    EXPECT_LT(b.percentual(), dia);
    EXPECT_GE(b.percentual(), kBrilhoMinimoPct);
}

TEST(Brilho, periodo_desconhecido_NAO_mexe_em_nada) {
    // Sem data o aparelho nao sabe, e mexer por palpite seria pior que
    // deixar como esta -- o brilho mudaria sozinho no boot e voltaria ao
    // primeiro fix.
    Brilho b;
    b.define_periodo(PeriodoDoDia::Noite);
    const std::uint8_t antes = b.percentual();
    b.define_periodo(PeriodoDoDia::Desconhecido);
    EXPECT_EQ(b.percentual(), antes);
    EXPECT_EQ(b.periodo(), PeriodoDoDia::Noite);
}

TEST(Brilho, o_ajuste_manual_vale_para_o_periodo_em_que_foi_feito) {
    // E o que faz o aparelho LEMBRAR: acerta-se uma vez de dia e uma vez de
    // noite, e a transicao seguinte ja vem certa. Com um preset so, seria
    // reajustar duas vezes por dia, para sempre.
    Brilho b;
    b.define_periodo(PeriodoDoDia::Dia);
    for (int i = 0; i < 4; ++i) { b.diminui(); }      // dia -> 80%
    ASSERT_EQ(b.percentual(), 80);

    b.define_periodo(PeriodoDoDia::Noite);
    for (int i = 0; i < 2; ++i) { b.diminui(); }      // noite -> 10%
    ASSERT_EQ(b.percentual(), 10);

    b.define_periodo(PeriodoDoDia::Dia);
    EXPECT_EQ(b.percentual(), 80) << "o ajuste de dia se perdeu";
    b.define_periodo(PeriodoDoDia::Noite);
    EXPECT_EQ(b.percentual(), 10) << "o ajuste de noite se perdeu";
}

TEST(Brilho, o_piso_vale_nos_dois_periodos) {
    Brilho b;
    for (const auto p : {PeriodoDoDia::Dia, PeriodoDoDia::Noite}) {
        b.define_periodo(p);
        for (int i = 0; i < 50; ++i) { b.diminui(); }
        EXPECT_EQ(b.percentual(), kBrilhoMinimoPct);
        EXPECT_GT(b.duty(), 0);
    }
}

TEST(Brilho, trocar_de_periodo_muda_o_duty_e_nao_so_o_rotulo) {
    Brilho b;
    b.define_periodo(PeriodoDoDia::Dia);
    const std::uint16_t d = b.duty();
    b.define_periodo(PeriodoDoDia::Noite);
    EXPECT_LT(b.duty(), d);
}

TEST(Brilho, o_pwm_fica_acima_da_faixa_que_assobia_e_cintila) {
    EXPECT_GE(kFrequenciaPwmHz, 20000U);
}

// --- presets vindos do coruja.cfg ---

TEST(Brilho, CarregaOsDoisPresetsDoArquivo) {
    Brilho b;
    b.define_presets(60, 25);
    b.define_periodo(PeriodoDoDia::Dia);
    EXPECT_EQ(b.percentual(), 60);
    b.define_periodo(PeriodoDoDia::Noite);
    EXPECT_EQ(b.percentual(), 25) << "os dois presets ficaram iguais";
}

TEST(Brilho, OsPresetsVoltamComoEntraram) {
    // Ida e volta pelo arquivo: o que o menu gravou e o que o boot le.
    Brilho b;
    b.define_presets(45, 15);
    EXPECT_EQ(b.pct_dia(), 45);
    EXPECT_EQ(b.pct_noite(), 15);
}

TEST(Brilho, PresetZeroNaoApagaATela) {
    // Um arquivo editado a mao com 0 nao pode deixar o aparelho cego:
    // sem tela nao ha como recuperar o brilho.
    Brilho b;
    b.define_presets(0, 0);
    b.define_periodo(PeriodoDoDia::Dia);
    EXPECT_EQ(b.percentual(), kBrilhoMinimoPct);
    EXPECT_GT(b.duty(), 0);
}

TEST(Brilho, PresetAcimaDeCemVaiParaOTeto) {
    Brilho b;
    b.define_presets(200, 200);
    EXPECT_EQ(b.pct_dia(), kBrilhoMaximoPct);
}

TEST(Brilho, PresetForaDoPassoVaiParaOMaisProximo) {
    Brilho b;
    b.define_presets(73, 72);
    EXPECT_EQ(b.pct_dia(), 75) << "arredondou para baixo em vez do mais proximo";
    EXPECT_EQ(b.pct_noite(), 70);
}

}  // namespace
