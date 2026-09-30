#include "led/PadraoLedOta.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

/// Percorre um periodo inteiro e diz se o LED apagou em algum momento.
bool pisca(const PadraoLedOta& p, std::uint32_t de, std::uint32_t ate) {
    bool houve_aceso = false;
    bool houve_apagado = false;
    for (std::uint32_t t = de; t <= ate; t += 10) {
        if (p.cor(t) == cores::kApagado) { houve_apagado = true; } else {
            houve_aceso = true;
        }
    }
    return houve_aceso && houve_apagado;
}

TEST(PadraoLedOta, as_fases_de_trabalho_sao_cianas) {
    // Nenhum estado de via usa ciano -- verde, ambar, rosa, vermelho e azul
    // estao todos tomados. E o que permite ao LED dizer "nao estou vigiando
    // a via, estou atualizando" sem uma sexta convencao a decorar.
    for (const auto f : {FaseOta::Conectando, FaseOta::Consultando,
                         FaseOta::Baixando, FaseOta::Verificando,
                         FaseOta::Gravando}) {
        PadraoLedOta p;
        p.define_fase(f, 0);
        bool viu_ciano = false;
        for (std::uint32_t t = 0; t < 2000; t += 10) {
            const Cor c = p.cor(t);
            if (c == cores::kCiano) { viu_ciano = true; }
            EXPECT_TRUE(c == cores::kCiano || c == cores::kApagado)
                << "cor de estado de via numa fase de OTA";
        }
        EXPECT_TRUE(viu_ciano);
    }
}

TEST(PadraoLedOta, gravar_e_verificar_ficam_FIXOS) {
    // E a janela da troca atomica do RF05.2, o unico momento em que
    // desligar o aparelho tem consequencia. Luz parada se le como
    // "ocupado, nao toque"; piscando, convida a mexer.
    for (const auto f : {FaseOta::Verificando, FaseOta::Gravando}) {
        PadraoLedOta p;
        p.define_fase(f, 0);
        EXPECT_FALSE(pisca(p, 0, 3000)) << "piscou na janela critica";
        EXPECT_EQ(p.cor(1234), cores::kCiano);
    }
}

TEST(PadraoLedOta, conectar_pisca_mais_devagar_que_baixar) {
    // A cadencia separa as fases sem gastar matiz novo, que e o recurso
    // escasso: o §4.1 exige 40 graus de separacao e ja tem cinco em uso.
    PadraoLedOta lento;
    lento.define_fase(FaseOta::Conectando, 0);
    PadraoLedOta rapido;
    rapido.define_fase(FaseOta::Baixando, 0);

    // Em 1 s, o de 1 Hz acende uma vez; o de 2 Hz, duas.
    auto transicoes = [](const PadraoLedOta& p) {
        int n = 0;
        bool antes = p.cor(0) != cores::kApagado;
        for (std::uint32_t t = 10; t <= 1000; t += 10) {
            const bool agora = p.cor(t) != cores::kApagado;
            if (agora != antes) { ++n; }
            antes = agora;
        }
        return n;
    };
    EXPECT_GT(transicoes(rapido), transicoes(lento));
}

TEST(PadraoLedOta, o_fim_bem_sucedido_e_verde_fixo) {
    for (const auto f : {FaseOta::Concluida, FaseOta::JaEmDia}) {
        PadraoLedOta p;
        p.define_fase(f, 0);
        EXPECT_EQ(p.cor(0), cores::kVerde);
        EXPECT_EQ(p.cor(5000), cores::kVerde);
        EXPECT_FALSE(pisca(p, 0, 3000));
    }
}

TEST(PadraoLedOta, a_falha_pisca_vermelho) {
    PadraoLedOta p;
    p.define_fase(FaseOta::Falhou, 0);
    EXPECT_TRUE(pisca(p, 0, 2000));
    bool viu_vermelho = false;
    for (std::uint32_t t = 0; t < 2000; t += 10) {
        if (p.cor(t) == cores::kVermelho) { viu_vermelho = true; }
    }
    EXPECT_TRUE(viu_vermelho);
}

TEST(PadraoLedOta, repetir_a_mesma_fase_nao_reinicia_o_piscar) {
    // O observador avisa a mesma fase varias vezes -- o download chama
    // `Baixando` a cada tentativa, e o progresso chega a cada pedaco. Se
    // cada aviso reiniciasse o relogio, o LED ficaria travado aceso.
    PadraoLedOta p;
    p.define_fase(FaseOta::Baixando, 0);
    const Cor em_400 = p.cor(400);
    p.define_fase(FaseOta::Baixando, 390);
    EXPECT_EQ(p.cor(400), em_400) << "o aviso repetido reiniciou o ciclo";
}

TEST(PadraoLedOta, trocar_de_fase_reinicia_o_piscar) {
    // O contrario do teste acima: a fase nova comeca acesa, para a mudanca
    // ser percebida.
    PadraoLedOta p;
    p.define_fase(FaseOta::Conectando, 0);
    p.define_fase(FaseOta::Baixando, 777);
    EXPECT_NE(p.cor(777), cores::kApagado) << "a fase nova comecou apagada";
}

}  // namespace
