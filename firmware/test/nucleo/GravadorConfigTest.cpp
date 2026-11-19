#include "nucleo/GravadorConfig.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "apoio/LoggerMock.h"
#include "apoio/fatfs/FatFsFalso.h"
#include "armazenamento/CartaoSd.h"
#include "nucleo/LeitorConfig.h"

namespace {

using namespace coruja;
using coruja::teste::LoggerMock;

/// Cartao de mentira: guarda o conteudo dos arquivos e anota a ordem das
/// operacoes, que aqui importa tanto quanto o resultado.
class CartaoFalso : public Armazenamento {
public:
    std::string cfg;
    bool tem_cfg = true;
    ErroCartao resposta_grava = ErroCartao::Nenhum;
    ErroCartao resposta_promove = ErroCartao::Nenhum;

    std::vector<std::string> chamadas;
    std::string tmp_escrito;
    std::string promoveu_de, promoveu_para, promoveu_reserva;

    ErroCartao le_arquivo(const char* nome, char* destino, std::size_t cap,
                          std::size_t* lidos, Logger&) override {
        chamadas.push_back(std::string("le:") + nome);
        if (!tem_cfg) {
            return ErroCartao::ArquivoAusente;
        }
        if (cfg.size() > cap) {
            return ErroCartao::ArquivoGrande;
        }
        std::memcpy(destino, cfg.data(), cfg.size());
        *lidos = cfg.size();
        return ErroCartao::Nenhum;
    }

    ErroCartao grava_arquivo(const char* nome, const char* conteudo,
                             std::size_t tamanho, Logger&) override {
        chamadas.push_back(std::string("grava:") + nome);
        if (resposta_grava != ErroCartao::Nenhum) {
            return resposta_grava;
        }
        tmp_escrito.assign(conteudo, tamanho);
        return ErroCartao::Nenhum;
    }

    ErroCartao promove(const char* de, const char* para, const char* reserva,
                       Logger&) override {
        chamadas.push_back(std::string("promove:") + de);
        promoveu_de = de;
        promoveu_para = para;
        promoveu_reserva = reserva;
        return resposta_promove;
    }

    ErroCartao acrescenta_arquivo(const char*, const char*, std::size_t,
                                  Logger&) override { return ErroCartao::Nenhum; }
    ErroCartao abre_para_escrita(const char*, Logger&) override {
        chamadas.push_back("abre");
        return ErroCartao::Nenhum;
    }
    bool escreve(const std::uint8_t*, std::size_t) override { return true; }
    ErroCartao conclui_escrita(Logger&) override { return ErroCartao::Nenhum; }
    void descarta_escrita(const char*, Logger&) override {}
};

constexpr const char* kCfgOriginal =
    "# comentario que precisa sobreviver\n"
    "wifi_ssid_1=casa\n"
    "wifi_senha_1=segredo\n"
    "url_base=https://ex/r.bin\n"
    "brilho_dia=100\n"
    "led=true\n";

Configuracao ajustada() {
    Configuracao c;
    std::snprintf(c.nome, sizeof(c.nome), "%s", "fusca");
    c.brilho_dia = 40;
    c.led_ligado = false;
    return c;
}

struct Cenario {
    CartaoFalso cartao;
    LoggerMock log;
    char trabalho[4096] = {};

    ResultadoGravacao grava(const Configuracao& c) {
        return grava_ajustes(cartao, c, trabalho, sizeof(trabalho), log);
    }
};

TEST(GravadorConfig, GravaEPromove) {
    Cenario c;
    c.cartao.cfg = kCfgOriginal;
    EXPECT_EQ(c.grava(ajustada()), ResultadoGravacao::Gravado);

    EXPECT_EQ(c.cartao.promoveu_de, "coruja.tmp");
    EXPECT_EQ(c.cartao.promoveu_para, "coruja.cfg");
    EXPECT_EQ(c.cartao.promoveu_reserva, "coruja.bak");
}

TEST(GravadorConfig, EscreveTudoAntesDePromover) {
    // A ordem e a garantia: enquanto o .tmp nao virou .cfg, uma queda de
    // energia nao custa nada.
    Cenario c;
    c.cartao.cfg = kCfgOriginal;
    c.grava(ajustada());
    const std::vector<std::string> esperado = {
        "le:coruja.cfg", "grava:coruja.tmp", "promove:coruja.tmp"};
    EXPECT_EQ(c.cartao.chamadas, esperado);
}

TEST(GravadorConfig, OQueVaiParaOCartaoEOArquivoInteiro) {
    Cenario c;
    c.cartao.cfg = kCfgOriginal;
    c.grava(ajustada());

    const auto& t = c.cartao.tmp_escrito;
    EXPECT_NE(t.find("# comentario que precisa sobreviver\n"), std::string::npos);
    EXPECT_NE(t.find("wifi_senha_1=segredo\n"), std::string::npos);

    const auto r = le_config(t.data(), t.size());
    EXPECT_EQ(r.config.brilho_dia, 40);
    EXPECT_FALSE(r.config.led_ligado);
    EXPECT_STREQ(r.config.nome, "fusca");
    ASSERT_EQ(r.config.n_redes, 1U);
    EXPECT_STREQ(r.config.redes[0].senha, "segredo");
}

TEST(GravadorConfig, SemCfgNoCartaoRecusaEmVezDeCriar) {
    // Criar um cfg so com ajustes daria um aparelho sem redes nem URLs
    // parecendo configurado, com o cartao errado na mao.
    Cenario c;
    c.cartao.tem_cfg = false;
    EXPECT_EQ(c.grava(ajustada()), ResultadoGravacao::ArquivoAusente);
    EXPECT_TRUE(c.cartao.tmp_escrito.empty());
    EXPECT_TRUE(c.cartao.promoveu_de.empty()) << "promoveu sem ter escrito";
}

TEST(GravadorConfig, FalhaDeEscritaNaoPromove) {
    // Promover depois de uma escrita falha trocaria o cfg bom por lixo.
    Cenario c;
    c.cartao.cfg = kCfgOriginal;
    c.cartao.resposta_grava = ErroCartao::FalhaDeEscrita;
    EXPECT_EQ(c.grava(ajustada()), ResultadoGravacao::FalhaDeEscrita);
    EXPECT_TRUE(c.cartao.promoveu_de.empty());
}

TEST(GravadorConfig, FalhaNaTrocaEReportada) {
    Cenario c;
    c.cartao.cfg = kCfgOriginal;
    c.cartao.resposta_promove = ErroCartao::FalhaDeRenomeacao;
    EXPECT_EQ(c.grava(ajustada()), ResultadoGravacao::FalhaDeTroca);
}

TEST(GravadorConfig, ArquivoMaiorQueOBufferRecusa) {
    Cenario c;
    c.cartao.cfg = std::string(4000, 'x') + "\n";  // passa da metade util
    EXPECT_EQ(c.grava(ajustada()), ResultadoGravacao::GrandeDemais);
    EXPECT_TRUE(c.cartao.tmp_escrito.empty());
    EXPECT_TRUE(c.cartao.promoveu_de.empty());
}

TEST(GravadorConfig, BufferMinusculoRecusaSemEscrever) {
    CartaoFalso cartao;
    LoggerMock log;
    cartao.cfg = kCfgOriginal;
    char trabalho[8];
    EXPECT_EQ(grava_ajustes(cartao, ajustada(), trabalho, sizeof(trabalho), log),
              ResultadoGravacao::GrandeDemais);
    EXPECT_TRUE(cartao.tmp_escrito.empty());
}

TEST(GravadorConfig, QuandoAReescritaNaoCabeNadaEGravado) {
    // O arquivo cabe na leitura, mas cresce ao ganhar as cinco chaves e
    // nao cabe na saida. Gravar o que coube deixaria no cartao um cfg
    // truncado no lugar de um cfg bom.
    CartaoFalso cartao;
    LoggerMock log;
    cartao.cfg = "url_base=https://ex/r.bin\n";
    char trabalho[256];  // 128 para ler (sobra), 128 para escrever (falta)
    EXPECT_EQ(grava_ajustes(cartao, ajustada(), trabalho, sizeof(trabalho), log),
              ResultadoGravacao::GrandeDemais);
    EXPECT_TRUE(cartao.tmp_escrito.empty()) << "gravou um arquivo truncado";
    EXPECT_TRUE(cartao.promoveu_de.empty());
}

TEST(GravadorConfig, GravarDuasVezesDaOMesmoArquivo) {
    Cenario c;
    c.cartao.cfg = kCfgOriginal;
    c.grava(ajustada());
    const std::string primeira = c.cartao.tmp_escrito;
    c.cartao.cfg = primeira;
    c.grava(ajustada());
    EXPECT_EQ(c.cartao.tmp_escrito, primeira)
        << "gravar o mesmo ajuste duas vezes mudou o arquivo";
}

// ===========================================================================
// A queda entre os dois renomeios, no cartao de verdade (CartaoSd + duble)
// ===========================================================================
//
// O `promove()` faz cfg -> bak e depois tmp -> cfg. Entre os dois o cartao
// fica SEM `coruja.cfg`, e quem lia so o cfg perdia as redes e as URLs para
// sempre: sem elas o aparelho nao consegue nem se atualizar para se
// consertar. A base de radares ja caia para a reserva; a configuracao nao.

/// O original mais a URL de versao: com ela a configuracao serve ao OTA, e
/// `carrega_configuracao` diz verdadeiro quando a acha.
const std::string kCfgCompleto =
    std::string(kCfgOriginal) + "url_versao=https://ex/versao.txt\n";

struct CartaoQueCai : public ::testing::Test {
    teste::FatFsFalso& fs = teste::FatFsFalso::instancia();
    LoggerMock         log;
    char               trabalho[kMaxTextoCfg] = {};
    char               texto[kMaxTextoCfg] = {};

    void SetUp() override {
        fs.reinicia();
        fs.volumes[0].monta = true;
        fs.volumes[0].arquivos[kArquivoCfg] = kCfgCompleto;
    }

    /// Grava `brilho_dia` e deixa a energia cair entre os dois renomeios.
    void grava_e_cai_no_meio_da_troca(std::uint8_t brilho) {
        CartaoSd sd;
        Configuracao c = le_config(kCfgCompleto.data(), kCfgCompleto.size()).config;
        c.brilho_dia = brilho;
        fs.queda_apos = "rename:coruja.cfg->coruja.bak";
        EXPECT_NE(grava_ajustes(sd, c, trabalho, sizeof trabalho, log),
                  ResultadoGravacao::Gravado);
        fs.religa();
        ASSERT_FALSE(fs.existe(0, kArquivoCfg))
            << "o teste nao reproduziu a janela entre os renomeios";
    }
};

TEST_F(CartaoQueCai, sem_o_cfg_o_boot_seguinte_le_a_reserva) {
    grava_e_cai_no_meio_da_troca(40);

    CartaoSd     depois;
    Configuracao lida;
    EXPECT_TRUE(carrega_configuracao(depois, texto, sizeof texto, &lida, log))
        << "o aparelho ficou sem configuracao utilizavel";
    ASSERT_EQ(lida.n_redes, 1U);
    EXPECT_STREQ(lida.redes[0].ssid, "casa");
    EXPECT_EQ(lida.brilho_dia, 100) << "a reserva e o arquivo de ANTES";
    EXPECT_TRUE(log.contem(Nivel::Warning, "coruja.bak"))
        << "operar pela reserva tem de ficar no log";
}

TEST_F(CartaoQueCai, a_gravacao_seguinte_parte_da_reserva_e_devolve_o_cfg) {
    grava_e_cai_no_meio_da_troca(40);

    CartaoSd     depois;
    Configuracao c = le_config(kCfgCompleto.data(), kCfgCompleto.size()).config;
    c.brilho_dia = 55;
    ASSERT_EQ(grava_ajustes(depois, c, trabalho, sizeof trabalho, log),
              ResultadoGravacao::Gravado);

    ASSERT_TRUE(fs.existe(0, kArquivoCfg));
    const std::string cfg = fs.conteudo(0, kArquivoCfg);
    EXPECT_NE(cfg.find("brilho_dia=55"), std::string::npos);
    EXPECT_NE(cfg.find("# comentario que precisa sobreviver"),
              std::string::npos)
        << "a reserva tinha o arquivo inteiro, e ele tinha de voltar inteiro";

    Configuracao lida;
    EXPECT_TRUE(carrega_configuracao(depois, texto, sizeof texto, &lida, log));
    EXPECT_EQ(lida.brilho_dia, 55);
}

TEST_F(CartaoQueCai, com_o_cfg_no_lugar_a_reserva_nem_e_lida) {
    // A reserva e o arquivo de ANTES. Preferi-la ao cfg presente desfaria o
    // ultimo ajuste a cada boot.
    fs.volumes[0].arquivos[kArquivoCfgBak] = "brilho_dia=10\n";
    CartaoSd     sd;
    Configuracao lida;
    carrega_configuracao(sd, texto, sizeof texto, &lida, log);
    EXPECT_EQ(lida.brilho_dia, 100);
    EXPECT_FALSE(log.contem(Nivel::Warning, "coruja.bak"));
}

TEST_F(CartaoQueCai, sem_cfg_e_sem_reserva_continua_sem_configuracao) {
    fs.volumes[0].arquivos.clear();
    CartaoSd     sd;
    Configuracao lida;
    EXPECT_FALSE(carrega_configuracao(sd, texto, sizeof texto, &lida, log));
    EXPECT_TRUE(log.contem(Nivel::Error, "coruja.cfg"));
}

}  // namespace
