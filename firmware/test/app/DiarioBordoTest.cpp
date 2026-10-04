#include "app/DiarioBordo.h"

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "apoio/LoggerMock.h"
#include "nucleo/FormatoLog.h"

namespace {

using namespace coruja;
using coruja::teste::LoggerMock;

/// Cartao em memoria. Guarda o conteudo por nome, com acrescimo de verdade,
/// para que os testes possam ler o arquivo inteiro no fim.
class CartaoFalso : public Armazenamento {
public:
    std::map<std::string, std::string> arquivos;
    bool falha_escrita = false;
    std::size_t leituras = 0;

    ErroCartao le_arquivo(const char* nome, char* destino,
                          std::size_t capacidade, std::size_t* lidos,
                          Logger&) override {
        ++leituras;
        const auto it = arquivos.find(nome);
        if (it == arquivos.end()) { return ErroCartao::ArquivoAusente; }
        if (it->second.size() >= capacidade) { return ErroCartao::ArquivoGrande; }
        std::memcpy(destino, it->second.data(), it->second.size());
        if (lidos != nullptr) { *lidos = it->second.size(); }
        return ErroCartao::Nenhum;
    }
    ErroCartao grava_arquivo(const char* nome, const char* conteudo,
                             std::size_t tamanho, Logger&) override {
        if (falha_escrita) { return ErroCartao::FalhaDeEscrita; }
        arquivos[nome] = std::string(conteudo, tamanho);
        return ErroCartao::Nenhum;
    }
    ErroCartao acrescenta_arquivo(const char* nome, const char* conteudo,
                                  std::size_t tamanho, Logger&) override {
        if (falha_escrita) { return ErroCartao::FalhaDeEscrita; }
        arquivos[nome] += std::string(conteudo, tamanho);
        return ErroCartao::Nenhum;
    }
    ErroCartao abre_para_escrita(const char*, Logger&) override {
        return ErroCartao::Nenhum;
    }
    bool escreve(const std::uint8_t*, std::size_t) override { return true; }
    ErroCartao conclui_escrita(Logger&) override { return ErroCartao::Nenhum; }
    void descarta_escrita(const char*, Logger&) override {}
    ErroCartao promove(const char*, const char*, const char*, Logger&) override {
        return ErroCartao::Nenhum;
    }

    bool tem(const std::string& nome) const {
        return arquivos.find(nome) != arquivos.end();
    }
    std::size_t linhas(const std::string& nome) const {
        const auto it = arquivos.find(nome);
        if (it == arquivos.end()) { return 0; }
        std::size_t n = 0;
        for (char c : it->second) { if (c == '\n') { ++n; } }
        return n;
    }
};

Telemetria em(std::uint8_t hora, std::uint8_t minuto, std::uint8_t segundo,
              float velocidade_kmh) {
    Telemetria t;
    t.lat = -19.8F; t.lon = -44.0F;
    t.velocidade_kmh = velocidade_kmh;
    t.rumo_graus = 90.0F; t.rumo_valido = true;
    t.ano = 2026; t.mes = 10; t.dia = 4;
    t.hora = hora; t.minuto = minuto; t.segundo = segundo;
    t.data_valida = true;
    return t;
}

Veredito vendo(float dist, std::uint8_t limite = 60) {
    Veredito v;
    v.tem_mais_proximo = true;
    v.mais_proximo = Ponto{-19.79F, -44.0F, limite, 0, TipoPonto::RadarFixo,
                           Sentido::Omnidirecional};
    v.dist_mais_proximo_m = dist;
    return v;
}

// --- infracoes ---

TEST(DiarioBordo, a_infracao_cria_o_arquivo_com_cabecalho) {
    CartaoFalso c; LoggerMock l;
    DiarioBordo d(c, l);

    d.passo(vendo(10.0F), em(17, 31, 10, 80.0F), true, 1000);
    d.passo(Veredito{}, em(17, 31, 11, 80.0F), true, 2000);

    ASSERT_TRUE(c.tem(kArquivoInfracoes));
    const auto& txt = c.arquivos[kArquivoInfracoes];
    EXPECT_EQ(txt.rfind("# coruja_gps infracoes v1", 0), 0u);
    EXPECT_NE(txt.find("2026-10-04T17:31:10Z;"), std::string::npos);
    EXPECT_EQ(c.linhas(kArquivoInfracoes), 3u) << "2 de cabecalho + 1 de dado";
}

TEST(DiarioBordo, o_cabecalho_nao_se_repete_em_arquivo_que_ja_existe) {
    CartaoFalso c; LoggerMock l;
    c.arquivos[kArquivoInfracoes] = std::string(kCabecalhoInfracoes) +
                                    "linha antiga\n";
    DiarioBordo d(c, l);

    d.passo(vendo(10.0F), em(17, 31, 10, 80.0F), true, 1000);
    d.passo(Veredito{}, em(17, 31, 11, 80.0F), true, 2000);

    EXPECT_EQ(c.linhas(kArquivoInfracoes), 4u) << "2 + antiga + nova";
}

TEST(DiarioBordo, sem_fix_nao_alimenta_o_detector_de_infracao) {
    CartaoFalso c; LoggerMock l;
    DiarioBordo d(c, l);
    d.passo(vendo(10.0F), em(17, 31, 10, 80.0F), false, 1000);
    d.passo(Veredito{}, em(17, 31, 11, 80.0F), false, 2000);
    EXPECT_FALSE(c.tem(kArquivoInfracoes));
}

// --- viagem ---

TEST(DiarioBordo, a_viagem_nasce_parada) {
    CartaoFalso c; LoggerMock l;
    DiarioBordo d(c, l);
    EXPECT_EQ(d.estado_viagem(), EstadoViagem::Parada);
    d.passo(Veredito{}, em(17, 30, 0, 60.0F), true, 1000);
    EXPECT_TRUE(c.arquivos.empty());
}

TEST(DiarioBordo, alternar_inicia_e_o_primeiro_fix_cria_o_arquivo) {
    CartaoFalso c; LoggerMock l;
    DiarioBordo d(c, l);

    d.alterna_viagem();
    EXPECT_EQ(d.estado_viagem(), EstadoViagem::Aguardando);

    d.passo(Veredito{}, em(17, 30, 42, 60.0F), true, 1000);
    EXPECT_EQ(d.estado_viagem(), EstadoViagem::Gravando);
    ASSERT_TRUE(c.tem("20261004_173042.log"));
    EXPECT_EQ(c.arquivos["20261004_173042.log"], kCabecalhoViagem);
}

TEST(DiarioBordo, cada_minuto_grava_um_ponto_e_salva_o_estado) {
    CartaoFalso c; LoggerMock l;
    DiarioBordo d(c, l);
    d.alterna_viagem();
    d.passo(Veredito{}, em(17, 30, 0, 72.0F), true, 0);
    d.passo(Veredito{}, em(17, 31, 0, 72.0F), true, 60000);

    EXPECT_EQ(c.linhas("20261004_173000.log"), 3u) << "2 de cabecalho + 1 ponto";
    ASSERT_TRUE(c.tem(kArquivoEstadoViagem));
    EXPECT_NE(c.arquivos[kArquivoEstadoViagem].find("ativa=1"),
              std::string::npos);
}

TEST(DiarioBordo, alternar_de_novo_encerra_e_desarma_o_estado) {
    CartaoFalso c; LoggerMock l;
    DiarioBordo d(c, l);
    d.alterna_viagem();
    d.passo(Veredito{}, em(17, 30, 0, 72.0F), true, 0);
    d.alterna_viagem();

    EXPECT_EQ(d.estado_viagem(), EstadoViagem::Parada);
    EXPECT_NE(c.arquivos[kArquivoEstadoViagem].find("ativa=0"),
              std::string::npos);
}

// --- retomada depois do corte de energia ---

TEST(DiarioBordo, retoma_dentro_da_janela_em_arquivo_novo_com_a_distancia) {
    CartaoFalso c; LoggerMock l;
    c.arquivos[kArquivoEstadoViagem] =
        "ativa=1\nutc=2026-10-04T12:00Z\ndist_km=180.45\n";
    DiarioBordo d(c, l);

    // Primeiro fix 80 minutos depois: dentro dos 120.
    d.passo(Veredito{}, em(13, 20, 5, 60.0F), true, 1000);

    EXPECT_EQ(d.estado_viagem(), EstadoViagem::Gravando);
    EXPECT_TRUE(c.tem("20261004_132005.log")) << "trecho novo, arquivo novo";
    EXPECT_NEAR(d.dist_viagem_km(), 180.45F, 0.01F) << "a distancia continua";
}

TEST(DiarioBordo, fora_da_janela_nao_retoma_e_apaga_a_bandeira) {
    CartaoFalso c; LoggerMock l;
    c.arquivos[kArquivoEstadoViagem] =
        "ativa=1\nutc=2026-10-04T07:00Z\ndist_km=180.45\n";
    DiarioBordo d(c, l);

    d.passo(Veredito{}, em(17, 30, 0, 60.0F), true, 1000);   // 10h30 depois

    EXPECT_EQ(d.estado_viagem(), EstadoViagem::Parada);
    EXPECT_NE(c.arquivos[kArquivoEstadoViagem].find("ativa=0"),
              std::string::npos)
        << "deixou a bandeira ligada, e a proxima energizacao tentaria de novo";
}

TEST(DiarioBordo, estado_inativo_no_cartao_nao_retoma) {
    CartaoFalso c; LoggerMock l;
    c.arquivos[kArquivoEstadoViagem] =
        "ativa=0\nutc=2026-10-04T17:00Z\ndist_km=180.45\n";
    DiarioBordo d(c, l);
    d.passo(Veredito{}, em(17, 10, 0, 60.0F), true, 1000);
    EXPECT_EQ(d.estado_viagem(), EstadoViagem::Parada);
}

TEST(DiarioBordo, a_retomada_e_tentada_uma_vez_so) {
    // Encerrar a viagem e seguir dirigindo nao pode faze-la renascer no
    // ciclo seguinte.
    CartaoFalso c; LoggerMock l;
    c.arquivos[kArquivoEstadoViagem] =
        "ativa=1\nutc=2026-10-04T17:00Z\ndist_km=5.0\n";
    DiarioBordo d(c, l);

    d.passo(Veredito{}, em(17, 10, 0, 60.0F), true, 1000);
    ASSERT_EQ(d.estado_viagem(), EstadoViagem::Gravando);
    d.alterna_viagem();
    ASSERT_EQ(d.estado_viagem(), EstadoViagem::Parada);

    for (std::uint32_t ms = 2000; ms < 20000; ms += 1000) {
        d.passo(Veredito{}, em(17, 10, 30, 60.0F), true, ms);
    }
    EXPECT_EQ(d.estado_viagem(), EstadoViagem::Parada) << "a viagem renasceu";
}

TEST(DiarioBordo, iniciar_pelo_menu_nao_herda_distancia_do_cartao) {
    // A retomada e automatica e so na energizacao. Clicar iniciar e pedido
    // explicito de viagem nova, e tem de comecar do zero.
    CartaoFalso c; LoggerMock l;
    c.arquivos[kArquivoEstadoViagem] =
        "ativa=1\nutc=2026-10-04T17:00Z\ndist_km=180.45\n";
    DiarioBordo d(c, l);

    d.alterna_viagem();
    d.passo(Veredito{}, em(17, 10, 0, 60.0F), true, 1000);

    EXPECT_FLOAT_EQ(d.dist_viagem_km(), 0.0F);
}

TEST(DiarioBordo, estado_ilegivel_nao_vira_leitura_de_cartao_a_4_Hz) {
    // Sem cartao, ou com o arquivo de estado corrompido, nenhuma das outras
    // barreiras impede nova tentativa: a viagem segue parada e nao ha estado
    // para zerar. Sem a bandeira seriam quatro leituras por segundo, para
    // sempre, por nada -- num meio de ciclos finitos.
    CartaoFalso c; LoggerMock l;
    DiarioBordo d(c, l);   // cartao vazio: le_arquivo devolve ArquivoAusente

    for (std::uint32_t ms = 1000; ms <= 20000; ms += 250) {
        d.passo(Veredito{}, em(17, 10, 0, 60.0F), true, ms);
    }

    EXPECT_LE(c.leituras, 1u) << "sondou o cartao " << c.leituras << " vezes";
    EXPECT_EQ(d.estado_viagem(), EstadoViagem::Parada);
}

// --- falhas de cartao ---

TEST(DiarioBordo, falha_de_escrita_nao_derruba_nem_trava) {
    CartaoFalso c; LoggerMock l;
    c.falha_escrita = true;
    DiarioBordo d(c, l);

    d.alterna_viagem();
    d.passo(vendo(10.0F), em(17, 30, 0, 80.0F), true, 0);
    d.passo(Veredito{}, em(17, 31, 0, 80.0F), true, 60000);

    // O estado interno segue coerente: o cartao falhou, o aparelho nao.
    EXPECT_EQ(d.estado_viagem(), EstadoViagem::Gravando);
    EXPECT_GT(l.contagem(Nivel::Error), 0u) << "falhou em silencio";
}

}  // namespace
