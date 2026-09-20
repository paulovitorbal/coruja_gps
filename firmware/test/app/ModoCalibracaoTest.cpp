#include "app/ModoCalibracao.h"

#include <gtest/gtest.h>

#include "apoio/EncoderMock.h"
#include "led/Calibracao.h"
#include "apoio/LedRgbMock.h"

namespace {

using namespace coruja;

constexpr auto kDir = EventoEncoder::GiroDireita;
constexpr auto kEsq = EventoEncoder::GiroEsquerda;
constexpr auto kClq = EventoEncoder::Clique;

void repete(ModoCalibracao& m, EventoEncoder e, int n) {
    for (int i = 0; i < n; ++i) {
        m.aplica(e);
    }
}

void vai_para(ModoCalibracao& m, ItemCalibracao alvo) {
    for (int i = 0; i < 10 && m.item() != alvo; ++i) {
        m.aplica(kClq);
    }
    ASSERT_EQ(m.item(), alvo);
}

// --- navegação ---

TEST(ModoCalibracao, ComecaNoVermelho) {
    ModoCalibracao m;
    EXPECT_EQ(m.item(), ItemCalibracao::Vermelho);
    EXPECT_EQ(m.cor(), (Cor{255, 0, 0}));
}

TEST(ModoCalibracao, CliqueAvancaNaOrdemDeJulgamento) {
    // Canais isolados primeiro: é olhando o vermelho sozinho que se julga se
    // o âmbar puxou demais para o verde.
    ModoCalibracao m;
    const ItemCalibracao esperado[] = {
        ItemCalibracao::Verde, ItemCalibracao::Azul, ItemCalibracao::Ambar,
        ItemCalibracao::Rosa,  ItemCalibracao::Vermelho};
    for (auto e : esperado) {
        m.aplica(kClq);
        EXPECT_EQ(m.item(), e);
    }
}

TEST(ModoCalibracao, OCicloSinalizaAoDarAVolta) {
    // É o momento de imprimir o resumo completo.
    ModoCalibracao m;
    for (int i = 0; i < 4; ++i) {
        m.aplica(kClq);
        EXPECT_FALSE(m.completou_ciclo()) << "sinalizou cedo no passo " << i;
    }
    m.aplica(kClq);
    EXPECT_TRUE(m.completou_ciclo());
    EXPECT_EQ(m.item(), ItemCalibracao::Vermelho);
}

TEST(ModoCalibracao, OSinalDeCicloNaoGruda) {
    ModoCalibracao m;
    repete(m, kClq, 5);
    ASSERT_TRUE(m.completou_ciclo());
    m.aplica(EventoEncoder::Nenhum);
    EXPECT_FALSE(m.completou_ciclo());
}

// --- ajuste ---

TEST(ModoCalibracao, GiroAjustaOCanalDoItemAtual) {
    ModoCalibracao m;
    vai_para(m, ItemCalibracao::Verde);
    const auto antes = m.duty(ItemCalibracao::Verde);
    m.aplica(kEsq);
    EXPECT_EQ(m.duty(ItemCalibracao::Verde), antes - ModoCalibracao::kPasso);
    m.aplica(kDir);
    EXPECT_EQ(m.duty(ItemCalibracao::Verde), antes);
}

TEST(ModoCalibracao, SaturaEmVezDeDarAVolta) {
    // Girar além do fim não pode saltar de máximo para mínimo num detente: a
    // mão espera um ajuste contínuo.
    ModoCalibracao m;
    repete(m, kDir, 100);
    EXPECT_EQ(m.duty(ItemCalibracao::Vermelho), 255);
    repete(m, kEsq, 100);
    EXPECT_EQ(m.duty(ItemCalibracao::Vermelho), 0);
    m.aplica(kEsq);
    EXPECT_EQ(m.duty(ItemCalibracao::Vermelho), 0) << "deu a volta no zero";
}

TEST(ModoCalibracao, CadaItemGuardaOSeuAjuste) {
    // Voltar atrás não pode perder o que já foi escolhido.
    ModoCalibracao m;
    repete(m, kEsq, 3);
    const auto verm = m.duty(ItemCalibracao::Vermelho);
    vai_para(m, ItemCalibracao::Azul);
    repete(m, kEsq, 7);
    const auto azul = m.duty(ItemCalibracao::Azul);

    repete(m, kClq, 3);  // volta ao vermelho pelo ciclo
    ASSERT_EQ(m.item(), ItemCalibracao::Vermelho);
    EXPECT_EQ(m.duty(ItemCalibracao::Vermelho), verm);
    EXPECT_EQ(m.duty(ItemCalibracao::Azul), azul);
}

TEST(ModoCalibracao, EventoNenhumNaoMexeEmNada) {
    ModoCalibracao m;
    const auto antes = m.cor();
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(m.aplica(EventoEncoder::Nenhum), antes);
    }
    EXPECT_EQ(m.item(), ItemCalibracao::Vermelho);
}

// --- as compostas ---

TEST(ModoCalibracao, NasCompostasOVermelhoFicaEmCemPorCento) {
    // É a forma como led/Calibracao.h guarda as cores: canal vermelho em 255
    // e só o secundário variando.
    ModoCalibracao m;
    vai_para(m, ItemCalibracao::Ambar);
    repete(m, kEsq, 5);
    EXPECT_EQ(m.cor().r, 255) << "o vermelho da composta saiu de 100%";
    EXPECT_EQ(m.cor().b, 0);
    EXPECT_EQ(m.cor().g, m.duty(ItemCalibracao::Ambar));

    vai_para(m, ItemCalibracao::Rosa);
    repete(m, kDir, 5);
    EXPECT_EQ(m.cor().r, 255);
    EXPECT_EQ(m.cor().g, 0);
    EXPECT_EQ(m.cor().b, m.duty(ItemCalibracao::Rosa));
}

TEST(ModoCalibracao, OsIniciaisSaoOsValoresMedidos) {
    // Recalibrar parte de onde se chegou, não de um palpite. Os valores vêm
    // de led/Calibracao.h, medidos em 2026-09-19.
    ModoCalibracao m;
    EXPECT_EQ(m.duty(ItemCalibracao::Ambar), calibracao::kAmbar.g);
    EXPECT_EQ(m.duty(ItemCalibracao::Rosa), calibracao::kRosa.b);
}

TEST(ModoCalibracao, RazaoAcompanhaODuty) {
    ModoCalibracao m;
    EXPECT_FLOAT_EQ(m.razao(ItemCalibracao::Vermelho), 1.0F);
    repete(m, kEsq, 51);  // 51 * 5 = 255
    EXPECT_EQ(m.duty(ItemCalibracao::Vermelho), 0);
    EXPECT_FLOAT_EQ(m.razao(ItemCalibracao::Vermelho), 0.0F);
}

TEST(ModoCalibracao, OsCanaisIsoladosAcendemSozinhos) {
    // Prova a fiação dos três canais, inclusive o verde, que o modo de teste
    // anterior nunca acendia.
    ModoCalibracao m;
    EXPECT_EQ(m.cor(), (Cor{255, 0, 0}));
    m.aplica(kClq);
    EXPECT_EQ(m.cor(), (Cor{0, 255, 0}));
    m.aplica(kClq);
    EXPECT_EQ(m.cor(), (Cor{0, 0, 255}));
}

TEST(ItemCalibracao_, TodosTemNomeEOCicloFecha) {
    auto i = ItemCalibracao::Vermelho;
    for (std::size_t n = 0; n < kQuantosItens; ++n) {
        EXPECT_STRNE(nome_item(i), "?");
        i = proximo(i);
    }
    EXPECT_EQ(i, ItemCalibracao::Vermelho) << "o ciclo não fechou em "
                                           << kQuantosItens << " passos";
    EXPECT_TRUE(e_composta(ItemCalibracao::Ambar));
    EXPECT_TRUE(e_composta(ItemCalibracao::Rosa));
    EXPECT_FALSE(e_composta(ItemCalibracao::Verde));
}

// --- a cadeia contra os mocks ---

TEST(CadeiaCalibracao, UmaSessaoAteOResumo) {
    teste::EncoderMock enc{kClq, kClq, kDir, kDir, kClq, kClq, kClq};
    teste::LedRgbMock led;
    ModoCalibracao m;
    int resumos = 0;

    while (!enc.vazio()) {
        led.define_cor(m.aplica(enc.proximo_evento()));
        if (m.completou_ciclo()) {
            ++resumos;
        }
    }
    EXPECT_EQ(resumos, 1) << "o resumo deve sair uma vez por volta";
    EXPECT_EQ(m.item(), ItemCalibracao::Vermelho);
    // Os dois giros no azul subiram 2 passos a partir de 255: satura.
    EXPECT_EQ(m.duty(ItemCalibracao::Azul), 255);
    EXPECT_EQ(led.escritas(), 7U);
}

}  // namespace
