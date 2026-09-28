#include "app/Aplicacao.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apoio/EncoderMock.h"
#include "apoio/LoggerMock.h"
#include "nucleo/LeitorConfig.h"

namespace {

using namespace coruja;
using coruja::teste::EncoderMock;
using coruja::teste::LoggerMock;

constexpr float kLat0 = -15.79F;
constexpr float kLon0 = -47.88F;

/// Mesmo simulador do PilotoAlertaTest: sentencas montadas na hora.
class SimuladorUart : public Uart {
public:
    void emite_rmc(float kmh, int hora_utc = 12) {
        char corpo[128];
        std::snprintf(corpo, sizeof corpo,
                      "$GNRMC,%02d3519.00,A,1547.400000,S,04752.800000,W,"
                      "%.2f,0.0,220926,,,A",
                      hora_utc, static_cast<double>(kmh / 1.852F));
        std::uint8_t cs = 0;
        for (const char* p = corpo + 1; *p != 0; ++p) {
            cs ^= static_cast<std::uint8_t>(*p);
        }
        char fim[8];
        std::snprintf(fim, sizeof fim, "*%02X\r\n", cs);
        const std::string s = std::string(corpo) + fim;
        saida.insert(saida.end(), s.begin(), s.end());
    }
    void escreve(const std::uint8_t*, std::size_t) override {}
    void define_baud(std::uint32_t) override {}
    std::size_t le(std::uint8_t* destino, std::size_t capacidade) override {
        const std::size_t n = saida.size() < capacidade ? saida.size()
                                                        : capacidade;
        for (std::size_t i = 0; i < n; ++i) { destino[i] = saida[i]; }
        saida.erase(saida.begin(), saida.begin() + static_cast<long>(n));
        return n;
    }
    std::vector<std::uint8_t> saida;
};

class LedMudo : public LedRgb {
public:
    void define_cor(const Cor& c) override { atual = c; }
    Cor cor_atual() const override { return atual; }
    Cor atual = cores::kApagado;
};

class BuzzerMudo : public Buzzer {
public:
    void define(bool l) override { ligado_ = l; }
    bool ligado() const override { return ligado_; }
    bool ligado_ = false;
};

class CartaoFalso : public Armazenamento {
public:
    std::string cfg = "url_base=https://ex/r.bin\nbrilho_dia=100\n"
                      "brilho_noite=20\nmodo_noturno=auto\n"
                      "volume_buzzer=100\nnome=fusca\n";
    ErroCartao le_arquivo(const char*, char* destino, std::size_t cap,
                          std::size_t* lidos, Logger&) override {
        if (cfg.size() > cap) { return ErroCartao::ArquivoGrande; }
        std::memcpy(destino, cfg.data(), cfg.size());
        *lidos = cfg.size();
        return ErroCartao::Nenhum;
    }
    ErroCartao grava_arquivo(const char*, const char* c, std::size_t n,
                             Logger&) override {
        tmp.assign(c, n);
        return resposta_grava;
    }
    ErroCartao promove(const char*, const char*, const char*,
                       Logger&) override {
        if (resposta_promove == ErroCartao::Nenhum) { cfg = tmp; }
        return resposta_promove;
    }
    ErroCartao acrescenta_arquivo(const char*, const char*, std::size_t,
                                  Logger&) override { return ErroCartao::Nenhum; }
    ErroCartao abre_para_escrita(const char*, Logger&) override {
        return ErroCartao::Nenhum;
    }
    bool escreve(const std::uint8_t*, std::size_t) override { return true; }
    ErroCartao conclui_escrita(Logger&) override { return ErroCartao::Nenhum; }
    void descarta_escrita(const char*, Logger&) override {}

    std::string tmp;
    ErroCartao resposta_grava = ErroCartao::Nenhum;
    ErroCartao resposta_promove = ErroCartao::Nenhum;
};

class AcoesEspias : public AcoesAplicacao {
public:
    void atualiza_base() override { ++bases; }
    void testa_alertas() override { ++testes; }
    unsigned bases = 0;
    unsigned testes = 0;
};

struct Bancada {
    SimuladorUart uart;
    LeitorGps     gps{uart};
    EncoderMock   encoder;
    LedMudo       led;
    BuzzerMudo    buzzer;
    PilotoAlerta  piloto{gps, led, buzzer};
    Brilho        brilho;
    CartaoFalso   cartao;
    AcoesEspias   acoes;
    LoggerMock    log;
    char          trabalho[4096] = {};
    Configuracao  inicial;

    Bancada() {
        const auto r = le_config(cartao.cfg.data(), cartao.cfg.size());
        inicial = r.config;
    }

    Aplicacao monta() {
        return Aplicacao{gps,    encoder, piloto,  brilho,   cartao,
                         acoes,  log,     inicial, trabalho, sizeof(trabalho)};
    }

    /// Roda o laco, com ou sem sentenca de GPS.
    void roda(Aplicacao& app, std::uint32_t de, std::uint32_t ate,
              float kmh, bool com_gps = true, int hora_utc = 12) {
        for (std::uint32_t t = de; t <= ate; t += 50) {
            if (com_gps && t % 250 == 0) { uart.emite_rmc(kmh, hora_utc); }
            app.passo(t);
        }
    }
};

// --- a pre-condicao chega ao menu ---

TEST(Aplicacao, ParadoOGiroAbreOMenu) {
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    app.passo(4050);
    EXPECT_TRUE(app.menu().aberto());
}

TEST(Aplicacao, AndandoOGiroNaoAbreOMenu) {
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 60.0F);
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    app.passo(4050);
    EXPECT_FALSE(app.menu().aberto());
}

TEST(Aplicacao, TresSegundosSaoExigidosAntesDeAbrir) {
    // Frear ate parar e girar no mesmo instante nao abre: a sustentacao
    // do RF05.1 e o que separa "parou" de "esta parando".
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 1000, 60.0F);
    b.roda(app, 1050, 2000, 0.0F);
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    app.passo(2050);
    EXPECT_FALSE(app.menu().aberto());
    b.roda(app, 2100, 5100, 0.0F);
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    app.passo(5150);
    EXPECT_TRUE(app.menu().aberto());
}

TEST(Aplicacao, SairAndandoFechaOMenu) {
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    app.passo(4050);
    ASSERT_TRUE(app.menu().aberto());
    b.roda(app, 4100, 5000, 50.0F);
    EXPECT_FALSE(app.menu().aberto());
}

// --- o alerta nao para ---

TEST(Aplicacao, OAlertaContinuaComOMenuAberto) {
    // O laco do GPS nao pode esperar o menu: sair andando com o menu na
    // tela e justamente o caso que o RF05.1 previne.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    app.passo(4050);
    ASSERT_TRUE(app.menu().aberto());
    const auto antes = b.gps.telemetria().velocidade_kmh;
    b.roda(app, 4100, 6000, 0.0F);
    EXPECT_TRUE(b.gps.tem_fix(6000)) << "o GPS parou de ser lido";
    EXPECT_FLOAT_EQ(b.gps.telemetria().velocidade_kmh, antes);
}

// --- ajuste chega ao brilho e ao cartao ---

TEST(Aplicacao, OArquivoValeDesdeAPrimeiraVolta) {
    // Sem isto o aparelho so obedeceria ao cartao depois que alguem
    // mexesse no encoder.
    Bancada b;
    b.cartao.cfg = "brilho_dia=40\nbrilho_noite=10\n";
    b.inicial = le_config(b.cartao.cfg.data(), b.cartao.cfg.size()).config;
    auto app = b.monta();
    EXPECT_EQ(b.brilho.pct_dia(), 40);
    EXPECT_EQ(b.brilho.pct_noite(), 10);
}

TEST(Aplicacao, OGiroMudaOBrilhoAntesDeGravar) {
    // A tela segue o giro na hora; a gravacao vem depois, ao fechar.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);
    b.encoder.enfileira(EventoEncoder::GiroDireita);  // abre
    b.encoder.enfileira(EventoEncoder::Clique);       // entra no brilho
    b.encoder.enfileira(EventoEncoder::GiroEsquerda); // 100 -> 95
    app.passo(4050);
    EXPECT_EQ(b.brilho.pct_dia(), 95);
    EXPECT_EQ(app.gravacoes(), 0U) << "gravou no meio da edicao";
}

TEST(Aplicacao, FecharGravaNoCartao) {
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    b.encoder.enfileira(EventoEncoder::Clique);
    b.encoder.enfileira(EventoEncoder::GiroEsquerda);
    app.passo(4050);
    b.roda(app, 4100, 5000, 50.0F);  // andar fecha
    ASSERT_EQ(app.gravacoes(), 1U);
    EXPECT_EQ(app.ultima_gravacao(), ResultadoGravacao::Gravado);

    const auto r = le_config(b.cartao.cfg.data(), b.cartao.cfg.size());
    EXPECT_EQ(r.config.brilho_dia, 95) << "o cartao nao recebeu o ajuste";
    EXPECT_STREQ(r.config.nome, "fusca") << "o resto do arquivo se perdeu";
}

TEST(Aplicacao, FalhaDeCartaoNaoDesfazOAjusteNaTela) {
    // Perder o cartao nao e razao para escurecer a tela de volta na cara
    // de quem acabou de ajustar.
    Bancada b;
    b.cartao.resposta_promove = ErroCartao::FalhaDeRenomeacao;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    b.encoder.enfileira(EventoEncoder::Clique);
    b.encoder.enfileira(EventoEncoder::GiroEsquerda);
    app.passo(4050);
    b.roda(app, 4100, 5000, 50.0F);
    EXPECT_EQ(app.ultima_gravacao(), ResultadoGravacao::FalhaDeTroca);
    EXPECT_EQ(b.brilho.pct_dia(), 95);
    EXPECT_GT(b.log.contagem(Nivel::Error), 0U) << "a falha nao foi registrada";
}

// --- acoes ---

TEST(Aplicacao, AtualizarBaseChegaAQuemSabeFazer) {
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    for (int i = 0; i < 3; ++i) {
        b.encoder.enfileira(EventoEncoder::GiroDireita);
    }
    b.encoder.enfileira(EventoEncoder::Clique);
    app.passo(4050);
    EXPECT_EQ(b.acoes.bases, 1U);
    EXPECT_EQ(b.acoes.testes, 0U);
}

// --- a fila do encoder ---

TEST(Aplicacao, NenhumDetenteSePerdeNumaVolta) {
    // O encoder produz mais rapido que o laco em um giro decidido. Um
    // detente perdido por volta seria um giro que nao aparece na tela.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);
    b.encoder.enfileira(EventoEncoder::GiroDireita);  // abre
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    app.passo(4050);
    EXPECT_EQ(app.menu().item(), ItemMenu::Volume) << "detente perdido";
}

// --- sem GPS ---

TEST(Aplicacao, NaBancadaSemGpsOMenuAbre) {
    // Garagem coberta: sem fix desde o boot conta como parado.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F, /*com_gps=*/false);
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    app.passo(4050);
    EXPECT_TRUE(app.menu().aberto());
}

// --- o periodo chega a quem precisa dele ---

TEST(Aplicacao, ANoiteTrocaOPresetDeBrilho) {
    // 03:00 UTC em Brasilia e meia-noite local.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 2000, 0.0F, true, /*hora_utc=*/3);
    EXPECT_EQ(b.brilho.percentual(), b.brilho.pct_noite());
    EXPECT_NE(b.brilho.pct_noite(), b.brilho.pct_dia())
        << "os presets sao iguais; o teste nao distingue nada";
}

TEST(Aplicacao, ODiaUsaOPresetDeDia) {
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 2000, 0.0F, true, /*hora_utc=*/12);
    EXPECT_EQ(b.brilho.percentual(), b.brilho.pct_dia());
}

TEST(Aplicacao, OMenuDizQualPresetEstaEditando) {
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F, true, /*hora_utc=*/3);
    EXPECT_STREQ(app.menu().rotulo(ItemMenu::Brilho), "brilho (noite)");
}

TEST(Aplicacao, ModoNoturnoForcadoVenceOCeu) {
    // A garagem coberta ao meio-dia: e o caso que o modo forcado existe
    // para atender, e ele so serve se ignorar o calculo.
    Bancada b;
    b.cartao.cfg = "brilho_dia=100\nbrilho_noite=20\nmodo_noturno=noite\n";
    b.inicial = le_config(b.cartao.cfg.data(), b.cartao.cfg.size()).config;
    auto app = b.monta();
    b.roda(app, 0, 2000, 0.0F, true, /*hora_utc=*/12);
    EXPECT_EQ(b.brilho.percentual(), 20) << "o meio-dia venceu o ajuste";
}

TEST(Aplicacao, PerderOFixAndandoNaoLiberaOMenu) {
    // Tunel a 60 km/h: o sinal some, mas o carro nao parou.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 60.0F);
    b.roda(app, 4050, 20000, 0.0F, /*com_gps=*/false);
    ASSERT_FALSE(b.gps.tem_fix(20000)) << "o fix nao chegou a se perder";
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    app.passo(20050);
    EXPECT_FALSE(app.menu().aberto());
}

}  // namespace
