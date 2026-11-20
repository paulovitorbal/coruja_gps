#include "rede/CaixaVarredura.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <set>
#include <string>

#include "apoio/LoggerMock.h"

namespace {

using namespace coruja;

Configuracao com_redes(std::initializer_list<const char*> ssids) {
    Configuracao cfg;
    for (const char* s : ssids) {
        std::snprintf(cfg.redes[cfg.n_redes].ssid,
                      sizeof cfg.redes[cfg.n_redes].ssid, "%s", s);
        ++cfg.n_redes;
    }
    return cfg;
}

// ====================================================== a escolha da rede

TEST(CaixaVarredura, sem_nenhuma_da_lista_visivel_nao_ha_escolhida) {
    Configuracao cfg = com_redes({"casa", "celular"});
    teste::LoggerMock log;
    CaixaVarredura caixa;
    const auto g = caixa.abre(cfg, log);

    caixa.viu(g, "vizinho", -50, 6);
    caixa.viu(g, "outro", -60, 11);

    EXPECT_FALSE(caixa.tem_escolhida());
    EXPECT_EQ(caixa.vistas(), 2U) << "as de fora da lista contam na varredura";
}

TEST(CaixaVarredura, escolhe_a_de_maior_prioridade_e_nao_a_primeira_vista) {
    // A varredura devolve os pontos de acesso em ordem arbitraria, entao
    // parar na primeira que casar daria a rede errada. O que vale e a ordem
    // da lista do `coruja.cfg` (ADR 0002), nao o sinal nem a chegada.
    Configuracao cfg = com_redes({"casa", "celular", "vizinho"});
    teste::LoggerMock log;
    CaixaVarredura caixa;
    const auto g = caixa.abre(cfg, log);

    caixa.viu(g, "vizinho", -30, 1);   // o mais forte, e o ultimo da lista
    caixa.viu(g, "celular", -80, 6);
    caixa.viu(g, "casa", -90, 11);

    ASSERT_TRUE(caixa.tem_escolhida());
    EXPECT_EQ(caixa.escolhida(), 0U) << "trocou a prioridade pelo sinal";
    EXPECT_EQ(caixa.rssi(), -90) << "o sinal e o DA ESCOLHIDA";
    EXPECT_EQ(caixa.canal(), 11U);
}

TEST(CaixaVarredura, a_de_maior_prioridade_vence_mesmo_vista_primeiro) {
    // O outro lado da mesma regra, e nao repeticao: no teste acima a de
    // maior prioridade chega por ultimo, entao "guarda sempre a ultima"
    // passaria nele. Aqui ela chega primeiro.
    Configuracao cfg = com_redes({"casa", "celular", "vizinho"});
    teste::LoggerMock log;
    CaixaVarredura caixa;
    const auto g = caixa.abre(cfg, log);

    caixa.viu(g, "casa", -90, 11);
    caixa.viu(g, "celular", -80, 6);
    caixa.viu(g, "vizinho", -30, 1);

    ASSERT_TRUE(caixa.tem_escolhida());
    EXPECT_EQ(caixa.escolhida(), 0U);
    EXPECT_EQ(caixa.rssi(), -90);
    EXPECT_EQ(caixa.canal(), 11U);
}

TEST(CaixaVarredura, a_mesma_rede_vista_duas_vezes_nao_troca_o_que_se_sabe) {
    // Dois pontos de acesso com o mesmo SSID, ou a mesma rede em duas
    // bandas: a primeira leitura e a que fica, como era antes da caixa.
    Configuracao cfg = com_redes({"casa"});
    teste::LoggerMock log;
    CaixaVarredura caixa;
    const auto g = caixa.abre(cfg, log);

    caixa.viu(g, "casa", -40, 1);
    caixa.viu(g, "casa", -70, 6);

    ASSERT_TRUE(caixa.tem_escolhida());
    EXPECT_EQ(caixa.rssi(), -40);
    EXPECT_EQ(caixa.canal(), 1U);
    EXPECT_EQ(caixa.vistas(), 2U);
}

TEST(CaixaVarredura, ssid_vazio_ou_nulo_e_ignorado) {
    // Rede oculta: o ponto de acesso responde sem o nome.
    Configuracao cfg = com_redes({"casa"});
    teste::LoggerMock log;
    CaixaVarredura caixa;
    const auto g = caixa.abre(cfg, log);

    caixa.viu(g, "", -40, 1);
    caixa.viu(g, nullptr, -40, 1);

    EXPECT_EQ(caixa.vistas(), 0U);
    EXPECT_FALSE(caixa.tem_escolhida());
}

TEST(CaixaVarredura, o_ssid_dos_vizinhos_nao_vai_para_o_log_acima_de_debug) {
    // O log sobe pela rede e o cartao sai do carro: o Wi-Fi de terceiros nao
    // responde pergunta nenhuma. Em 27/10/2026 eram 284 linhas e 17 KB.
    Configuracao cfg = com_redes({"casa"});
    teste::LoggerMock log;
    CaixaVarredura caixa;
    const auto g = caixa.abre(cfg, log);
    caixa.viu(g, "wifi-do-vizinho", -50, 6);

    for (const auto& e : log.entradas()) {
        if (e.nivel > Nivel::Debug) {
            EXPECT_EQ(e.mensagem.find("wifi-do-vizinho"), std::string::npos)
                << e.mensagem;
        }
    }
}

// =============================== a varredura que responde depois do prazo

TEST(CaixaVarredura, rede_vista_com_outra_geracao_nao_conta) {
    // O caso que da nome a classe: a varredura estourou o prazo, quem
    // esperava foi embora, e o driver segue chamando. Sem a geracao, esta
    // chamada escreveria num quadro de pilha ja reaproveitado.
    Configuracao cfg = com_redes({"casa"});
    teste::LoggerMock log;
    CaixaVarredura caixa;
    const auto g = caixa.abre(cfg, log);

    caixa.viu(g + 1, "casa", -40, 1);

    EXPECT_EQ(caixa.vistas(), 0U);
    EXPECT_FALSE(caixa.tem_escolhida());
}

TEST(CaixaVarredura, rede_vista_depois_de_abandonar_nao_conta) {
    Configuracao cfg = com_redes({"casa"});
    teste::LoggerMock log;
    CaixaVarredura caixa;
    const auto g = caixa.abre(cfg, log);
    caixa.abandona();

    caixa.viu(g, "casa", -40, 1);

    EXPECT_EQ(caixa.vistas(), 0U);
    EXPECT_FALSE(caixa.tem_escolhida());
}

TEST(CaixaVarredura, a_varredura_velha_nao_contamina_a_nova) {
    Configuracao antiga = com_redes({"casa"});
    teste::LoggerMock log;
    CaixaVarredura caixa;
    const auto velha = caixa.abre(antiga, log);
    caixa.viu(velha, "casa", -40, 1);
    caixa.abandona();

    Configuracao nova = com_redes({"celular"});
    const auto g = caixa.abre(nova, log);
    caixa.viu(velha, "casa", -40, 1);   // a atrasada

    EXPECT_EQ(caixa.vistas(), 0U);
    EXPECT_FALSE(caixa.tem_escolhida());

    caixa.viu(g, "celular", -60, 6);
    ASSERT_TRUE(caixa.tem_escolhida());
    EXPECT_EQ(caixa.escolhida(), 0U);
}

TEST(CaixaVarredura, a_geracao_nunca_e_zero_nem_se_repete) {
    // Zero quer dizer "nenhuma varredura em curso". Uma geracao zero faria
    // toda chamada atrasada ser aceita -- e a volta do contador acontece
    // depois de 2^32 varreduras, que o programa nao exercita.
    Configuracao cfg = com_redes({"casa"});
    teste::LoggerMock log;
    CaixaVarredura caixa(0xFFFFFFFEU);
    std::set<std::uint32_t> vistas;
    for (int i = 0; i < 4; ++i) {
        const auto g = caixa.abre(cfg, log);
        EXPECT_NE(g, 0U);
        EXPECT_TRUE(vistas.insert(g).second) << "geracao repetida: " << g;
        caixa.abandona();
    }
}

}  // namespace
