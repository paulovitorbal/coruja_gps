#include "nucleo/GravadorConfig.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "apoio/LoggerMock.h"
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
    "volume_buzzer=100\n";

Configuracao ajustada() {
    Configuracao c;
    std::snprintf(c.nome, sizeof(c.nome), "%s", "fusca");
    c.brilho_dia = 40;
    c.volume_buzzer = 50;
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
    EXPECT_EQ(r.config.volume_buzzer, 50);
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

}  // namespace
