#include "menu/MenuAjustes.h"

#include "display/Brilho.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace coruja;

constexpr bool kParado = true;
constexpr bool kAndando = false;

/// Menu ja aberto no item indicado, para nao repetir a abertura em todo
/// teste. Devolve o tempo corrente.
class Cenario {
public:
    Cenario() : menu_(Configuracao{}) {}

    MenuAjustes& menu() { return menu_; }

    AcaoMenu gira_direita() { return passo(EventoEncoder::GiroDireita); }
    AcaoMenu gira_esquerda() { return passo(EventoEncoder::GiroEsquerda); }
    AcaoMenu clica() { return passo(EventoEncoder::Clique); }
    AcaoMenu ocioso(std::uint32_t quanto_ms) {
        agora_ += quanto_ms;
        return menu_.avalia(EventoEncoder::Nenhum, kParado, agora_);
    }
    AcaoMenu anda() {
        agora_ += 100;
        return menu_.avalia(EventoEncoder::Nenhum, kAndando, agora_);
    }

    void abre() { gira_direita(); }

    void vai_ate(ItemMenu alvo) {
        for (int i = 0; i < 20 && menu_.item() != alvo; ++i) {
            gira_direita();
        }
        ASSERT_EQ(menu_.item(), alvo);
    }

    std::string valor(ItemMenu i) {
        char buf[32];
        menu_.valor(i, buf, sizeof(buf));
        return buf;
    }

private:
    AcaoMenu passo(EventoEncoder e) {
        agora_ += 100;
        return menu_.avalia(e, kParado, agora_);
    }
    MenuAjustes menu_;
    std::uint32_t agora_ = 1000;
};

// --- abertura ---

TEST(MenuAjustes, AbreAoGirarComOCarroParado) {
    Cenario c;
    EXPECT_FALSE(c.menu().aberto());
    c.gira_direita();
    EXPECT_EQ(c.menu().estado(), EstadoMenu::Navegando);
    EXPECT_EQ(c.menu().item(), ItemMenu::Brilho);
}

TEST(MenuAjustes, GirarEmMovimentoNaoAbre) {
    MenuAjustes m{Configuracao{}};
    m.avalia(EventoEncoder::GiroDireita, kAndando, 1000);
    EXPECT_FALSE(m.aberto());
}

TEST(MenuAjustes, OCliqueNaoAbreOMenu) {
    // O clique e do OTA (RF05). Se abrisse o menu, o eixo facil de
    // pressionar sem intencao tiraria o velocimetro da tela.
    Cenario c;
    c.clica();
    EXPECT_FALSE(c.menu().aberto());
}

TEST(MenuAjustes, AOAberturaNaoPulaOPrimeiroItem) {
    // O giro que abre e o giro que abre, nao um passo de navegacao.
    Cenario c;
    c.gira_direita();
    EXPECT_EQ(c.menu().item(), ItemMenu::Brilho);
}

// --- fechamento ---

TEST(MenuAjustes, AndarFechaNaHora) {
    Cenario c;
    c.abre();
    EXPECT_EQ(c.anda(), AcaoMenu::Nenhuma);
    EXPECT_FALSE(c.menu().aberto());
}

TEST(MenuAjustes, AndarNoMeioDeUmaEdicaoFechaEGrava) {
    // O que ja foi editado vale; a gravacao sai junto com o fechamento.
    Cenario c;
    c.abre();
    c.clica();          // entra em Brilho
    c.gira_esquerda();  // muda o valor
    EXPECT_EQ(c.anda(), AcaoMenu::Gravar);
    EXPECT_FALSE(c.menu().aberto());
}

TEST(MenuAjustes, FechaSozinhoPorInatividade) {
    Cenario c;
    c.abre();
    EXPECT_EQ(c.ocioso(kTimeoutMenuMs - 1), AcaoMenu::Nenhuma);
    EXPECT_TRUE(c.menu().aberto());
    EXPECT_EQ(c.ocioso(1), AcaoMenu::Nenhuma);
    EXPECT_FALSE(c.menu().aberto());
}

TEST(MenuAjustes, CadaEventoAdiaOTimeout) {
    // Olhar so o 'aberto' no fim nao serve: se o menu fechasse por
    // timeout, o giro seguinte o reabriria e o fechamento seria
    // invisivel. O que denuncia e a navegacao voltar ao primeiro item.
    Cenario c;
    c.abre();
    for (int i = 0; i < 5; ++i) {
        c.ocioso(kTimeoutMenuMs - 1000);
        ASSERT_TRUE(c.menu().aberto()) << "fechou na volta " << i;
        c.gira_direita();
    }
    EXPECT_TRUE(c.menu().aberto());
    EXPECT_EQ(c.menu().item(), static_cast<ItemMenu>(5 % kItensMenu))
        << "a navegacao foi reiniciada: o menu fechou e reabriu";
}

TEST(MenuAjustes, SairFechaEGravaSeMudou) {
    Cenario c;
    c.abre();
    c.vai_ate(ItemMenu::Sair);
    EXPECT_EQ(c.clica(), AcaoMenu::Nenhuma) << "gravou sem ter mudado nada";
    EXPECT_FALSE(c.menu().aberto());
}

TEST(MenuAjustes, GirarNoTetoNaoContaComoMudanca) {
    // Girar para cima no maximo nao muda valor nenhum. Gravar aqui
    // gastaria um ciclo de escrita do cartao por nada, toda vez.
    Cenario c;
    c.abre();
    c.clica();
    ASSERT_EQ(c.menu().ajustes().brilho_dia, kBrilhoMaximoPct);
    c.gira_direita();
    c.clica();
    c.vai_ate(ItemMenu::Sair);
    EXPECT_EQ(c.clica(), AcaoMenu::Nenhuma);
}

TEST(MenuAjustes, SairSemMudarNadaNaoGastaCicloDeEscrita) {
    Cenario c;
    c.abre();
    c.clica();          // entra em brilho
    c.clica();          // sai sem girar
    c.vai_ate(ItemMenu::Sair);
    EXPECT_EQ(c.clica(), AcaoMenu::Nenhuma);
}

// --- navegacao ---

TEST(MenuAjustes, GirarAndaPelosItensECircula) {
    Cenario c;
    c.abre();
    for (std::size_t i = 1; i < kItensMenu; ++i) {
        c.gira_direita();
    }
    EXPECT_EQ(c.menu().item(), ItemMenu::Sair);
    c.gira_direita();
    EXPECT_EQ(c.menu().item(), ItemMenu::Brilho) << "nao circulou";
}

TEST(MenuAjustes, CircularParaTras) {
    Cenario c;
    c.abre();
    c.gira_esquerda();
    EXPECT_EQ(c.menu().item(), ItemMenu::Sair);
}

// --- edicao ---

TEST(MenuAjustes, EditaOBrilhoEmPassosDeCinco) {
    Cenario c;
    c.abre();
    c.clica();
    EXPECT_EQ(c.menu().estado(), EstadoMenu::Editando);
    c.gira_esquerda();
    EXPECT_EQ(c.menu().ajustes().brilho_dia, 95);
    c.gira_direita();
    EXPECT_EQ(c.menu().ajustes().brilho_dia, 100);
}

TEST(MenuAjustes, OBrilhoNaoPassaDoTetoNemDoPiso) {
    Cenario c;
    c.abre();
    c.clica();
    for (int i = 0; i < 30; ++i) { c.gira_direita(); }
    EXPECT_EQ(c.menu().ajustes().brilho_dia, kBrilhoMaximoPct);
    for (int i = 0; i < 40; ++i) { c.gira_esquerda(); }
    EXPECT_EQ(c.menu().ajustes().brilho_dia, kBrilhoMinimoPct)
        << "o visor apagado tira a referencia para recuperar o brilho";
}

TEST(MenuAjustes, OBrilhoEditadoEODoPeriodoVigente) {
    Cenario c;
    c.menu().define_periodo(PeriodoDoDia::Noite);
    c.abre();
    c.clica();
    c.gira_direita();
    EXPECT_EQ(c.menu().ajustes().brilho_noite, 25);
    EXPECT_EQ(c.menu().ajustes().brilho_dia, 100) << "mexeu no preset errado";
}

TEST(MenuAjustes, OVolumeNuncaDesceAbaixoDeCinquenta) {
    // O buzzer nao silencia: e a razao de o aparelho existir.
    Cenario c;
    c.abre();
    c.vai_ate(ItemMenu::Volume);
    c.clica();
    for (int i = 0; i < 20; ++i) { c.gira_esquerda(); }
    EXPECT_EQ(c.menu().ajustes().volume_buzzer, kVolumeMinimo);
}

TEST(MenuAjustes, OVolumeAndaDeDezEmDez) {
    Cenario c;
    c.abre();
    c.vai_ate(ItemMenu::Volume);
    c.clica();
    c.gira_esquerda();
    EXPECT_EQ(c.menu().ajustes().volume_buzzer, 90);
}

TEST(MenuAjustes, ModoNoturnoPercorreAsTresPosicoesSemCircular) {
    Cenario c;
    c.abre();
    c.vai_ate(ItemMenu::ModoNoturno);
    c.clica();
    EXPECT_EQ(c.valor(ItemMenu::ModoNoturno), "auto");
    c.gira_direita();
    EXPECT_EQ(c.valor(ItemMenu::ModoNoturno), "dia");
    c.gira_direita();
    EXPECT_EQ(c.valor(ItemMenu::ModoNoturno), "noite");
    c.gira_direita();
    EXPECT_EQ(c.valor(ItemMenu::ModoNoturno), "noite") << "circulou";
    c.gira_esquerda();
    c.gira_esquerda();
    c.gira_esquerda();
    EXPECT_EQ(c.valor(ItemMenu::ModoNoturno), "auto");
}

TEST(MenuAjustes, OCliqueConfirmaEVoltaANavegar) {
    Cenario c;
    c.abre();
    c.clica();
    ASSERT_EQ(c.menu().estado(), EstadoMenu::Editando);
    c.clica();
    EXPECT_EQ(c.menu().estado(), EstadoMenu::Navegando);
    c.gira_direita();
    EXPECT_EQ(c.menu().item(), ItemMenu::ModoNoturno) << "ainda editando";
}

// --- acoes ---

TEST(MenuAjustes, AtualizarBaseFechaOMenuAntesDeDisparar) {
    // O OTA suspende a leitura do GPS; o menu nao pode ficar por baixo
    // esperando um evento que nao vem.
    Cenario c;
    c.abre();
    c.vai_ate(ItemMenu::AtualizarBase);
    EXPECT_EQ(c.clica(), AcaoMenu::AtualizarBase);
    EXPECT_FALSE(c.menu().aberto());
}

TEST(MenuAjustes, TestarAlertasNaoFechaOMenu) {
    // Quem testa quer repetir e comparar.
    Cenario c;
    c.abre();
    c.vai_ate(ItemMenu::TestarAlertas);
    EXPECT_EQ(c.clica(), AcaoMenu::TestarAlertas);
    EXPECT_TRUE(c.menu().aberto());
    EXPECT_EQ(c.clica(), AcaoMenu::TestarAlertas);
}

TEST(MenuAjustes, InformacaoVoltaComQualquerEvento) {
    Cenario c;
    c.abre();
    c.vai_ate(ItemMenu::Informacao);
    c.clica();
    EXPECT_EQ(c.menu().estado(), EstadoMenu::Informando);
    c.gira_direita();
    EXPECT_EQ(c.menu().estado(), EstadoMenu::Navegando);
    EXPECT_EQ(c.menu().item(), ItemMenu::Informacao) << "o giro tambem andou";
}

// --- o que vai para a tela ---

TEST(MenuAjustes, RotuloDoBrilhoDizQualPresetEstaSendoEditado) {
    Cenario c;
    EXPECT_STREQ(c.menu().rotulo(ItemMenu::Brilho), "brilho (dia)");
    c.menu().define_periodo(PeriodoDoDia::Noite);
    EXPECT_STREQ(c.menu().rotulo(ItemMenu::Brilho), "brilho (noite)");
}

TEST(MenuAjustes, ItensDeAcaoNaoTemValor) {
    Cenario c;
    EXPECT_EQ(c.valor(ItemMenu::AtualizarBase), "");
    EXPECT_EQ(c.valor(ItemMenu::Sair), "");
}

TEST(MenuAjustes, ValorCabeEmBufferPequenoSemEstourar) {
    MenuAjustes m{Configuracao{}};
    char buf[3] = {'a', 'a', 'a'};
    m.valor(ItemMenu::Brilho, buf, sizeof(buf));
    EXPECT_EQ(std::string(buf).size(), 2U);
}

}  // namespace
