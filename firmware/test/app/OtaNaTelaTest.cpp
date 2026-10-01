#include "app/OtaNaTela.h"

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

    void retangulo(int x, int y, int l, int a, Cor565 c) override {
        retangulos.push_back({x, y, l, a, c});
    }
    void texto(int, int, const char* s, Fonte, Cor565, Alinhamento) override {
        textos.emplace_back(s);
    }
    void icone(int, int, Icone) override {}
    void apresenta() override {}

    void limpa() { retangulos.clear(); textos.clear(); }
    bool tem(const std::string& p) const {
        for (const auto& t : textos) {
            if (t.find(p) != std::string::npos) { return true; }
        }
        return false;
    }
    bool tem_barra() const {
        for (const auto& r : retangulos) {
            if (r.cor == paleta::kBarraAmbar) { return true; }
        }
        return false;
    }
};

class LedEspiao : public LedRgb {
public:
    void define_cor(const Cor& c) override { atual = c; ++trocas; }
    Cor cor_atual() const override { return atual; }
    Cor atual = cores::kApagado;
    unsigned trocas = 0;
};

class PausaFalsa : public Pausa {
public:
    std::uint32_t agora = 0;
    void espera_ms(std::uint32_t ms) override { agora += ms; }
    std::uint32_t agora_ms() override { return agora; }
};

struct Bancada {
    VisorEspiao visor;
    LedEspiao   led;
    PausaFalsa  relogio;
    OtaNaTela   ponte{visor, led, relogio};
};

TEST(OtaNaTela, desenha_e_acende_a_cada_mudanca_de_fase) {
    // A razao de existir: o `executa()` e bloqueante e pode levar dezenas
    // de segundos. Sem alguem desenhando de DENTRO, a tela congelaria no
    // ultimo quadro e o usuario nao saberia se trabalha ou travou.
    Bancada b;
    b.ponte.fase(FaseOta::Conectando, 1);
    EXPECT_TRUE(b.visor.tem("CONECTANDO"));
    EXPECT_GT(b.led.trocas, 0U) << "o LED nao acompanhou";
}

TEST(OtaNaTela, o_progresso_enche_a_barra) {
    Bancada b;
    b.ponte.fase(FaseOta::Baixando, 1);
    b.visor.limpa();
    b.ponte.progresso(5000, 20000);
    EXPECT_TRUE(b.visor.tem("25%"));
    EXPECT_TRUE(b.visor.tem_barra());
}

TEST(OtaNaTela, sair_do_download_zera_a_barra) {
    // Sem zerar, a barra da tentativa anterior ficaria cheia por baixo da
    // fase nova -- dizendo que o que falhou tinha terminado.
    Bancada b;
    b.ponte.fase(FaseOta::Baixando, 1);
    b.ponte.progresso(19000, 20000);
    ASSERT_TRUE(b.visor.tem("95%"));

    b.visor.limpa();
    b.ponte.fase(FaseOta::Falhou, 1);
    EXPECT_EQ(b.ponte.estado().recebidos, 0U);
    EXPECT_EQ(b.ponte.estado().total, 0U);
    EXPECT_FALSE(b.visor.tem_barra()) << "a barra da tentativa anterior sobrou";
    EXPECT_FALSE(b.visor.tem("95%"));
}

TEST(OtaNaTela, uma_nova_tentativa_recomeca_a_barra_do_zero) {
    // O caso real: a tentativa 1 chega a 60% e o cartao recusa a escrita; a
    // tentativa 2 comeca de novo. A barra tem de voltar.
    Bancada b;
    b.ponte.fase(FaseOta::Baixando, 1);
    b.ponte.progresso(12000, 20000);
    b.ponte.fase(FaseOta::Baixando, 2);
    EXPECT_EQ(b.ponte.estado().recebidos, 0U)
        << "a tentativa nova herdou o progresso da anterior";
}

TEST(OtaNaTela, mantem_faz_o_LED_piscar_sem_novidade_nenhuma) {
    // As fases de rede passam segundos sem nenhum aviso. Se a cor so fosse
    // recalculada nas mudancas, o LED ficaria parado justamente nelas.
    Bancada b;
    b.ponte.fase(FaseOta::Conectando, 1);
    const Cor primeira = b.led.atual;

    bool mudou = false;
    for (int i = 0; i < 200; ++i) {
        b.relogio.agora += 10;
        b.ponte.mantem();
        if (!(b.led.atual == primeira)) { mudou = true; }
    }
    EXPECT_TRUE(mudou) << "o LED nao piscou entre as notificacoes";
}

TEST(OtaNaTela, a_janela_critica_fica_com_o_LED_fixo) {
    // Gravando e a troca atomica do RF05.2. Luz parada se le como
    // "ocupado, nao toque".
    Bancada b;
    b.ponte.fase(FaseOta::Gravando, 1);
    for (int i = 0; i < 300; ++i) {
        b.relogio.agora += 10;
        b.ponte.mantem();
        ASSERT_EQ(b.led.atual, cores::kCiano) << "piscou na gravacao";
    }
}

TEST(OtaNaTela, o_fim_bem_sucedido_deixa_o_LED_verde) {
    Bancada b;
    b.ponte.fase(FaseOta::Concluida, 1);
    EXPECT_EQ(b.led.atual, cores::kVerde);
}

TEST(OtaNaTela, a_falha_deixa_o_LED_vermelho_piscando) {
    Bancada b;
    b.ponte.fase(FaseOta::Falhou, 1);
    bool viu_vermelho = false;
    bool viu_apagado = false;
    for (int i = 0; i < 200; ++i) {
        b.relogio.agora += 10;
        b.ponte.mantem();
        if (b.led.atual == cores::kVermelho) { viu_vermelho = true; }
        if (b.led.atual == cores::kApagado) { viu_apagado = true; }
    }
    EXPECT_TRUE(viu_vermelho);
    EXPECT_TRUE(viu_apagado) << "ficou fixo em vez de piscar";
}


// --- o motivo da falha ---

TEST(OtaNaTela, o_motivo_chega_a_tela) {
    VisorEspiao v; LedEspiao led; PausaFalsa p;
    OtaNaTela ponte(v, led, p);

    ponte.fase(FaseOta::Falhou, 1);
    v.limpa();
    ponte.falhou("SEM WI-FI");

    EXPECT_TRUE(v.tem("SEM WI-FI"));
    EXPECT_EQ(ponte.estado().fase, FaseOta::Falhou);
}

TEST(OtaNaTela, uma_tentativa_NOVA_nao_herda_o_motivo_da_anterior) {
    // O caso real, e e o que o usuario relatou: clicou duas vezes. A
    // primeira falhou por um motivo, a segunda por outro -- e entre o
    // `fase(Falhou)`, que parte de dentro do orquestrador, e o `falhou()`,
    // que so chega quando `executa()` retorna, a tela mostraria o motivo da
    // tentativa PASSADA. Motivo errado e pior que motivo nenhum: manda o
    // usuario consertar o que nao esta quebrado.
    VisorEspiao v; LedEspiao led; PausaFalsa p;
    OtaNaTela ponte(v, led, p);

    ponte.fase(FaseOta::Falhou, 1);
    ponte.falhou("SEM WI-FI");

    ponte.fase(FaseOta::Conectando, 1);   // segunda tentativa comecando
    v.limpa();
    ponte.fase(FaseOta::Falhou, 1);

    EXPECT_FALSE(v.tem("SEM WI-FI")) << "o motivo velho sobreviveu";
    EXPECT_TRUE(v.tem("FALHOU"));
}

TEST(OtaNaTela, o_sucesso_depois_de_uma_falha_nao_carrega_motivo) {
    VisorEspiao v; LedEspiao led; PausaFalsa p;
    OtaNaTela ponte(v, led, p);

    ponte.fase(FaseOta::Falhou, 1);
    ponte.falhou("SEM WI-FI");
    ponte.fase(FaseOta::Concluida, 1);

    EXPECT_EQ(ponte.estado().motivo, nullptr);
}

}  // namespace
