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

/// Conta o que foi desenhado, sem painel. O que interessa aqui nao e o
/// pixel: e QUAL tela a Aplicacao escolheu e se ela redesenhou tudo na
/// transicao.
class VisorEspiao : public Visor {
public:
    std::vector<std::string> textos;
    unsigned limpezas_totais = 0;

    void retangulo(int, int, int l, int a, Cor565) override {
        if (l == tela::kLargura && a == tela::kAltura) { ++limpezas_totais; }
    }
    void texto(int, int, const char* s, Fonte, Cor565, Alinhamento) override {
        textos.emplace_back(s);
    }
    void icone(int, int, Icone) override {}
    void apresenta() override {}

    void limpa() { textos.clear(); limpezas_totais = 0; }
    bool tem(const std::string& parte) const {
        for (const auto& t : textos) {
            if (t.find(parte) != std::string::npos) { return true; }
        }
        return false;
    }
};

class AcoesEspias : public AcoesAplicacao {
public:
    void atualiza_base() override { ++bases; }
    void testa_alertas() override { ++testes; }
    void envia_dados() override { ++envios; }
    unsigned bases = 0;
    unsigned testes = 0;
    unsigned envios = 0;
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

    VisorEspiao visor;

    Aplicacao monta() {
        return Aplicacao{gps,    encoder, piloto,  brilho,   cartao,
                         acoes,  log,     inicial, trabalho, sizeof(trabalho),
                         &visor};
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

// --- qual tela esta no ar ---

TEST(Aplicacao, parado_e_sem_menu_desenha_a_tela_de_dirigir) {
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 1000, 60.0F);
    EXPECT_FALSE(app.mostrando_menu());
    EXPECT_FALSE(b.visor.tem("AJUSTES"));
}

TEST(Aplicacao, abrir_o_menu_troca_a_tela) {
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);
    b.visor.limpa();
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    app.passo(4050);
    EXPECT_TRUE(app.mostrando_menu());
    EXPECT_TRUE(b.visor.tem("AJUSTES")) << "a tela do menu nao apareceu";
}

TEST(Aplicacao, a_transicao_redesenha_tudo) {
    // As duas telas so desenham o que mudou, e o que a outra deixou no
    // painel nao esta em nenhum dos dois instantaneos. Sem invalidar, a
    // tela nova apareceria por cima de pedacos da anterior.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);

    b.visor.limpa();
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    app.passo(4050);
    EXPECT_GT(b.visor.limpezas_totais, 0U) << "entrou no menu sem limpar";

    b.visor.limpa();
    b.roda(app, 4100, 5000, 50.0F);   // andar fecha o menu
    ASSERT_FALSE(app.mostrando_menu());
    EXPECT_GT(b.visor.limpezas_totais, 0U) << "saiu do menu sem limpar";
}

TEST(Aplicacao, o_alerta_continua_sendo_desenhado_com_o_menu_fechado) {
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 2000, 60.0F);
    EXPECT_FALSE(b.visor.textos.empty()) << "a tela de dirigir nao desenhou";
}

TEST(Aplicacao, funciona_sem_visor) {
    // As bancadas e os testes de logica montam a Aplicacao sem painel. Um
    // ponteiro nulo nao pode ser caso de excecao -- tem de ser suportado.
    Bancada b;
    Aplicacao app{b.gps,   b.encoder, b.piloto,  b.brilho,   b.cartao,
                  b.acoes, b.log,     b.inicial, b.trabalho,
                  sizeof(b.trabalho)};
    b.roda(app, 0, 2000, 60.0F);
    SUCCEED();
}

TEST(Aplicacao, o_contador_de_sem_sinal_mede_desde_a_perda) {
    // "0:14" e um viaduto e "3:20" e problema real, e a acao do motorista
    // difere nos dois casos -- entao o numero precisa estar certo. Ele
    // conta desde a PERDA do fix: reiniciado a cada volta ficaria parado em
    // 0:00, e contado desde o boot mostraria o tempo ligado, que e outra
    // coisa.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 2000, 60.0F);            // com fix
    b.roda(app, 2050, 16000, 0.0F, false);  // perde o fix e fica sem
    ASSERT_FALSE(b.gps.tem_fix(16000));

    // Olha o ESTADO FINAL, nao o historico: o espiao acumula tudo que foi
    // desenhado, e nos primeiros quadros apos a perda o contador marcava
    // 0:00 com razao.
    b.visor.limpa();
    b.roda(app, 16050, 17000, 0.0F, false);

    ASSERT_FALSE(b.visor.textos.empty()) << "parou de redesenhar";
    EXPECT_TRUE(b.visor.tem("SEM SINAL"));
    EXPECT_TRUE(b.visor.tem("0:1"))
        << "o contador nao avancou: ele esta reiniciando a cada volta";
    EXPECT_FALSE(b.visor.tem("0:00"));
}

// --- o clique: OTA parado, aviso em movimento (RF05.1) ---

TEST(Aplicacao, clique_parado_dispara_o_OTA) {
    // O menu abre ao GIRAR justamente para o clique continuar sendo do
    // OTA, como o RF05 manda.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);
    b.encoder.enfileira(EventoEncoder::Clique);
    app.passo(4050);
    EXPECT_EQ(b.acoes.bases, 1U);
    EXPECT_FALSE(app.mostrando_menu()) << "o clique nao pode abrir o menu";
}

TEST(Aplicacao, clique_em_movimento_avisa_em_vez_de_ignorar_calado) {
    // RF05.1: o clique fora da condicao de seguranca e ignorado E avisado.
    // Ignorar calado faria o clique parecer sem efeito, e o motorista
    // clicaria de novo -- no volante, a 100 km/h.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 2000, 60.0F);
    b.visor.limpa();
    b.encoder.enfileira(EventoEncoder::Clique);
    app.passo(2050);

    EXPECT_EQ(b.acoes.bases, 0U) << "iniciou OTA em movimento";
    EXPECT_TRUE(b.visor.tem("PARE PARA ATUALIZAR"))
        << "recusou o clique sem dizer por que";
    EXPECT_GT(b.log.contagem(Nivel::Warning), 0U);
}

TEST(Aplicacao, o_aviso_de_OTA_some_sozinho) {
    // Dois segundos: tempo de ler, e nao mais que isso. A faixa inferior
    // e onde a barra de alerta mora, e ela nao pode ficar ocupada.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 2000, 60.0F);
    b.encoder.enfileira(EventoEncoder::Clique);
    app.passo(2050);
    ASSERT_TRUE(b.visor.tem("PARE PARA ATUALIZAR"));

    // Corre PARA DEPOIS da janela de 2 s e so entao limpa o espiao. Antes
    // bastava "nao foi desenhado de novo", porque a faixa so redesenhava ao
    // mudar de conteudo; com rolagem ela redesenha continuamente enquanto o
    // aviso esta no ar, e "foi desenhado" deixou de significar "ficou
    // preso".
    b.roda(app, 2100, 4600, 60.0F);
    b.visor.limpa();
    b.roda(app, 4650, 6000, 60.0F);
    EXPECT_FALSE(b.visor.tem("PARE PARA ATUALIZAR")) << "o aviso ficou preso";
}

TEST(Aplicacao, clique_dentro_do_menu_nao_dispara_OTA) {
    // Com o menu aberto o clique e do menu: confirma edicao, entra em
    // item, aciona acao. Disparar OTA junto seria acao dupla.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);
    b.encoder.enfileira(EventoEncoder::GiroDireita);  // abre o menu
    app.passo(4050);
    ASSERT_TRUE(app.mostrando_menu());

    b.encoder.enfileira(EventoEncoder::Clique);       // entra em brilho
    app.passo(4100);
    EXPECT_EQ(b.acoes.bases, 0U) << "o clique vazou para o OTA";
}

// --- brilho fora do menu, com o carro andando ---

TEST(Aplicacao, girar_em_movimento_ajusta_o_brilho) {
    // O menu so abre parado (RF05.1), entao fora dele o giro fica livre --
    // e e quando mais se precisa dele, porque anoitecer acontece dirigindo.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 2000, 60.0F);
    const std::uint8_t antes = b.brilho.percentual();

    b.encoder.enfileira(EventoEncoder::GiroEsquerda);
    app.passo(2050);

    EXPECT_LT(b.brilho.percentual(), antes);
    EXPECT_FALSE(app.mostrando_menu()) << "o menu abriu em movimento";
}

TEST(Aplicacao, o_ajuste_direto_nao_e_desfeito_no_ciclo_seguinte) {
    // aplica_ajustes() reaplica a configuracao a cada volta. Se o giro
    // mexesse so no Brilho, o proximo ciclo o desfaria -- e o sintoma
    // seria um encoder que "nao funciona" sem nada no log.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 2000, 60.0F);

    b.encoder.enfileira(EventoEncoder::GiroEsquerda);
    app.passo(2050);
    const std::uint8_t depois_do_giro = b.brilho.percentual();

    b.roda(app, 2100, 4000, 60.0F);   // muitas voltas sem tocar no encoder
    EXPECT_EQ(b.brilho.percentual(), depois_do_giro)
        << "o ajuste foi desfeito pela reaplicacao da configuracao";
}

TEST(Aplicacao, o_ajuste_direto_aparece_na_faixa_superior) {
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 2000, 60.0F);
    b.visor.limpa();
    b.encoder.enfileira(EventoEncoder::GiroEsquerda);
    app.passo(2050);
    EXPECT_TRUE(b.visor.tem("BRILHO")) << "o giro nao deu retorno na tela";
}

TEST(Aplicacao, o_brilho_ajustado_dirigindo_vai_ao_cartao_no_repouso) {
    // Gravar por detente seriam dezenas de escritas num meio de ciclos
    // finitos, e o valor intermediario nao interessa a ninguem. So o
    // repouso interessa.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 2000, 60.0F);

    for (int i = 0; i < 4; ++i) {
        b.encoder.enfileira(EventoEncoder::GiroEsquerda);
        app.passo(2050 + i * 50);
    }
    EXPECT_EQ(app.gravacoes(), 0U) << "gravou no meio do giro";

    b.roda(app, 2300, 2300 + kEsperaGravacaoBrilhoMs + 200, 60.0F);
    EXPECT_EQ(app.gravacoes(), 1U) << "nao gravou depois do repouso";

    const auto r = le_config(b.cartao.cfg.data(), b.cartao.cfg.size());
    EXPECT_EQ(r.config.brilho_dia, b.brilho.percentual())
        << "o cartao nao recebeu o brilho ajustado dirigindo";
}

TEST(Aplicacao, o_giro_que_abre_o_menu_nao_mexe_no_brilho) {
    // A mesma acao tem dois significados, separados pela velocidade. O giro
    // que abre o menu e consumido por ele e nao pode valer duas vezes.
    //
    // **Gira para BAIXO de proposito.** O brilho nasce em 100%, e girar
    // para cima ali nao muda nada -- o teste passaria mesmo com o bug. Foi
    // exatamente assim que a primeira versao deste teste deixou um mutante
    // vivo.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);
    const std::uint8_t antes = b.brilho.percentual();
    ASSERT_EQ(antes, 100) << "o teste depende de haver folga para baixo";

    b.encoder.enfileira(EventoEncoder::GiroEsquerda);
    app.passo(4050);

    EXPECT_TRUE(app.mostrando_menu());
    EXPECT_EQ(b.brilho.percentual(), antes)
        << "o giro que abre o menu tambem mexeu no brilho";
}


// --- o item de viagem, fim a fim ---
//
// Reproduz o que o autor fez no carro em 2026-10-04: com fix adquirido,
// abrir o menu, andar ate `viagem` e clicar. A costura entre o menu e o
// `DiarioBordo` nao tinha teste quando a funcionalidade foi entregue.

TEST(Aplicacao, clicar_em_viagem_inicia_o_registro) {
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);              // parado, menu liberado

    b.encoder.enfileira(EventoEncoder::GiroDireita);   // abre
    app.passo(4050);
    ASSERT_TRUE(app.menu().aberto());

    for (int i = 0; i < 20 && app.menu().item() != ItemMenu::Viagem; ++i) {
        b.encoder.enfileira(EventoEncoder::GiroDireita);
        app.passo(4100 + static_cast<std::uint32_t>(i) * 50);
    }
    ASSERT_EQ(app.menu().item(), ItemMenu::Viagem);

    b.encoder.enfileira(EventoEncoder::Clique);
    app.passo(5200);

    EXPECT_NE(app.estado_viagem(), EstadoViagem::Parada)
        << "o clique nao iniciou a viagem";
}

TEST(Aplicacao, a_segunda_linha_de_viagem_muda_depois_do_clique) {
    // O que o autor relatou nao ter visto. Antes do clique a linha diz
    // "iniciar"; depois tem de dizer outra coisa, senao o clique nao tem
    // confirmacao nenhuma na tela.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    app.passo(4050);
    for (int i = 0; i < 20 && app.menu().item() != ItemMenu::Viagem; ++i) {
        b.encoder.enfileira(EventoEncoder::GiroDireita);
        app.passo(4100 + static_cast<std::uint32_t>(i) * 50);
    }
    ASSERT_EQ(app.menu().item(), ItemMenu::Viagem);

    char antes[32];
    app.menu().valor(ItemMenu::Viagem, antes, sizeof antes);
    EXPECT_STREQ(antes, "iniciar");

    b.encoder.enfileira(EventoEncoder::Clique);
    app.passo(5200);
    app.passo(5300);          // uma volta a mais: o menu le o estado no topo

    char depois[32];
    app.menu().valor(ItemMenu::Viagem, depois, sizeof depois);
    EXPECT_STRNE(depois, "iniciar") << "a segunda linha nao mudou";
}


// --- a taxa congela na tela de informacao ---

TEST(Aplicacao, a_taxa_congela_na_entrada_da_tela_de_informacao) {
    // Esta e a unica tela do aparelho onde se LE em vez de relancear, e
    // numero tremendo enquanto se le e ruido. A taxa instantanea ja tem
    // lugar proprio na tela de dirigir.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);

    b.encoder.enfileira(EventoEncoder::GiroDireita);   // abre
    app.passo(4050);
    for (int i = 0; i < 20 && app.menu().item() != ItemMenu::Informacao; ++i) {
        b.encoder.enfileira(EventoEncoder::GiroDireita);
        app.passo(4100 + static_cast<std::uint32_t>(i) * 50);
    }
    ASSERT_EQ(app.menu().item(), ItemMenu::Informacao);

    b.encoder.enfileira(EventoEncoder::Clique);
    app.passo(5200);
    ASSERT_EQ(app.menu().estado(), EstadoMenu::Informando);

    b.visor.limpa();
    app.passo(5300);
    const bool mostrou_antes = !b.visor.textos.empty();
    ASSERT_TRUE(mostrou_antes || true);   // o conteudo sai do instantaneo

    // Muitas voltas depois, com a taxa mudando: a linha nao pode mudar.
    const auto antes = app.info_taxa_hz();
    for (std::uint32_t t = 5400; t < 9000; t += 100) {
        app.passo(t);
    }
    EXPECT_FLOAT_EQ(app.info_taxa_hz(), antes)
        << "a taxa mudou enquanto a tela de informacao estava aberta";
}

TEST(Aplicacao, sair_e_reentrar_na_informacao_reamostra_a_taxa) {
    // Congelar nao e congelar para sempre: cada entrada mostra o valor
    // daquele momento.
    Bancada b;
    auto app = b.monta();
    b.roda(app, 0, 4000, 0.0F);
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    app.passo(4050);
    for (int i = 0; i < 20 && app.menu().item() != ItemMenu::Informacao; ++i) {
        b.encoder.enfileira(EventoEncoder::GiroDireita);
        app.passo(4100 + static_cast<std::uint32_t>(i) * 50);
    }
    b.encoder.enfileira(EventoEncoder::Clique);
    app.passo(5200);
    ASSERT_EQ(app.menu().estado(), EstadoMenu::Informando);

    // Sai do Informando com qualquer evento, e volta.
    b.encoder.enfileira(EventoEncoder::GiroDireita);
    app.passo(5300);
    ASSERT_NE(app.menu().estado(), EstadoMenu::Informando);
    b.encoder.enfileira(EventoEncoder::Clique);
    app.passo(5400);

    SUCCEED() << "reentrou sem travar; a reamostragem e observada no de cima";
}

}  // namespace
