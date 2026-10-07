#include "log/LoggerCartao.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "apoio/LoggerMock.h"
#include "armazenamento/Armazenamento.h"

namespace {

using namespace coruja;

/// Cartão de mentira que só sabe acrescentar — é tudo que o logger usa.
/// Os demais métodos existem porque a interface pede, e abortam se alguém
/// os chamar: o logger que passar a usá-los está fazendo mais do que deve.
class CartaoDeLog : public Armazenamento {
public:
    std::string       arquivo;
    /// O conteúdo separado POR ARQUIVO. Existe desde que o logger passou a
    /// desviar para `remessa.log`: um dublê que junta tudo num só texto não
    /// consegue dizer se a linha saiu no arquivo certo, que é exatamente o
    /// que o desvio precisa garantir.
    std::map<std::string, std::string> por_nome;
    std::vector<std::size_t> escritas;   ///< tamanho de cada acréscimo aceito
    ErroCartao        resposta = ErroCartao::Nenhum;
    /// Loga a cada acréscimo, para provocar reentrância por `registra()`.
    Logger*           realimenta = nullptr;
    /// Chama `descarrega()` a cada acréscimo — reentrância pela porta da
    /// frente, que é pública e o `main` usa.
    LoggerCartao*     redescarrega = nullptr;

    ErroCartao acrescenta_arquivo(const char* nome, const char* conteudo,
                                  std::size_t tamanho, Logger&) override {
        nome_recebido = nome;
        if (realimenta != nullptr) {
            realimenta->error("cartao", "falhei ao gravar");
        }
        if (redescarrega != nullptr && chamadas_reentrantes < 5) {
            ++chamadas_reentrantes;
            redescarrega->descarrega();
        }
        if (resposta != ErroCartao::Nenhum) { return resposta; }
        por_nome[nome].append(conteudo, tamanho);
        arquivo.append(conteudo, tamanho);
        escritas.push_back(tamanho);
        return ErroCartao::Nenhum;
    }

    std::string nome_recebido;
    int chamadas_reentrantes = 0;

    ErroCartao le_arquivo(const char*, char*, std::size_t, std::size_t*,
                          Logger&) override { std::abort(); }
    ErroCartao grava_arquivo(const char*, const char*, std::size_t,
                             Logger&) override { std::abort(); }
    ErroCartao abre_para_escrita(const char*, Logger&) override { std::abort(); }
    bool escreve(const std::uint8_t*, std::size_t) override { std::abort(); }
    ErroCartao conclui_escrita(Logger&) override { std::abort(); }
    void descarta_escrita(const char*, Logger&) override { std::abort(); }
    ErroCartao promove(const char*, const char*, const char*,
                       Logger&) override { std::abort(); }
};

struct Bancada {
    teste::LoggerMock console;
    CartaoDeLog       cartao;
    LoggerCartao      log{console, cartao, "coruja.log"};

    Bancada() { log.grava_no_cartao(true); }

    std::size_t linhas_no_arquivo() const {
        std::size_t n = 0;
        for (const char c : cartao.arquivo) { if (c == '\n') { ++n; } }
        return n;
    }
};

// ================================================= encadeamento e filtragem

TEST(LoggerCartao, repassa_sempre_ao_seguinte_mesmo_desligado) {
    Bancada b;
    b.log.grava_no_cartao(false);
    b.log.info("teste", "oi");
    EXPECT_EQ(b.console.entradas().size(), 1U);
    EXPECT_TRUE(b.cartao.arquivo.empty());
}

TEST(LoggerCartao, desligado_nao_toca_no_cartao) {
    Bancada b;
    b.log.grava_no_cartao(false);
    for (int i = 0; i < 50; ++i) { b.log.error("teste", "erro"); }
    EXPECT_TRUE(b.cartao.escritas.empty());
}

TEST(LoggerCartao, abaixo_do_nivel_minimo_nao_entra_no_arquivo) {
    Bancada b;
    b.log.define_nivel_minimo(Nivel::Warning);
    b.log.info("teste", "irrelevante");
    b.log.descarrega();
    EXPECT_TRUE(b.cartao.arquivo.empty());
}

TEST(LoggerCartao, usa_o_nome_de_arquivo_que_recebeu) {
    Bancada b;
    b.log.error("teste", "x");
    EXPECT_EQ(b.cartao.nome_recebido, "coruja.log");
}

// ===================================================== quando descarrega

TEST(LoggerCartao, info_fica_no_buffer_e_warning_descarrega) {
    // Info a cada fix encheria o cartao de escritas; aviso e erro sao raros e
    // sao justamente os que nao podem se perder num travamento.
    Bancada b;
    b.log.info("teste", "primeira");
    b.log.info("teste", "segunda");
    EXPECT_TRUE(b.cartao.arquivo.empty()) << "gravou antes da hora";
    b.log.warning("teste", "terceira");
    EXPECT_EQ(b.linhas_no_arquivo(), 3U) << "o aviso tem de levar o buffer junto";
}

TEST(LoggerCartao, desligar_descarrega_o_que_estava_pendente) {
    Bancada b;
    b.log.info("teste", "pendente");
    b.log.grava_no_cartao(false);
    EXPECT_EQ(b.linhas_no_arquivo(), 1U);
}

TEST(LoggerCartao, descarregar_sem_nada_nao_escreve) {
    Bancada b;
    b.log.descarrega();
    EXPECT_TRUE(b.cartao.escritas.empty());
}

// ============================================ R-46: o buraco silencioso

TEST(LoggerCartao, gravacao_que_falha_PRESERVA_o_buffer) {
    // REGRESSAO do R-46. A primeira versao zerava o buffer ANTES de gravar,
    // para ele nao ficar preso -- e trocou "preso" por "perdido em silencio".
    // Durante o download da base a gravacao falha sempre, de proposito, e
    // cada tentativa jogava fora o log acumulado, inclusive linhas de ANTES
    // do download. Medido na epoca: 14 linhas sumiram de uma execucao real.
    Bancada b;
    b.cartao.resposta = ErroCartao::FalhaDeEscrita;
    b.log.info("teste", "antes do download");
    b.log.warning("teste", "durante");      // tenta gravar e falha
    EXPECT_TRUE(b.cartao.arquivo.empty());

    b.cartao.resposta = ErroCartao::Nenhum;  // o cartao volta
    b.log.descarrega();
    EXPECT_EQ(b.linhas_no_arquivo(), 2U)
        << "as linhas de antes da falha tinham de sobreviver";
    EXPECT_NE(b.cartao.arquivo.find("antes do download"), std::string::npos);
}

TEST(LoggerCartao, falhas_repetidas_nao_acumulam_perda) {
    Bancada b;
    b.cartao.resposta = ErroCartao::FalhaDeEscrita;
    for (int i = 0; i < 10; ++i) { b.log.warning("teste", "tentativa"); }
    b.cartao.resposta = ErroCartao::Nenhum;
    b.log.descarrega();
    EXPECT_EQ(b.linhas_no_arquivo(), 10U);
}

TEST(LoggerCartao, buffer_cheio_descarta_e_DECLARA_a_perda) {
    // Buraco declarado e diagnostico; buraco silencioso e armadilha -- faz
    // quem le concluir que o evento nao aconteceu.
    Bancada b;
    b.cartao.resposta = ErroCartao::FalhaDeEscrita;
    // Enche muito alem dos 4 KiB com o cartao recusando.
    for (int i = 0; i < 400; ++i) {
        b.log.info("origem", "mensagem de tamanho razoavel para encher o buffer");
    }
    b.cartao.resposta = ErroCartao::Nenhum;
    b.log.descarrega();
    EXPECT_NE(b.cartao.arquivo.find("linha(s) perdida(s) por buffer cheio"),
              std::string::npos)
        << "descartou em silencio";
}

TEST(LoggerCartao, sem_perda_nao_ha_aviso_de_perda) {
    Bancada b;
    b.log.info("teste", "curta");
    b.log.descarrega();
    EXPECT_EQ(b.cartao.arquivo.find("perdida"), std::string::npos);
}

TEST(LoggerCartao, o_aviso_de_perda_sai_uma_vez_so) {
    Bancada b;
    b.cartao.resposta = ErroCartao::FalhaDeEscrita;
    for (int i = 0; i < 400; ++i) {
        b.log.info("origem", "mensagem de tamanho razoavel para encher o buffer");
    }
    b.cartao.resposta = ErroCartao::Nenhum;
    b.log.descarrega();
    b.log.info("teste", "depois");
    b.log.descarrega();
    std::size_t avisos = 0;
    std::size_t p = b.cartao.arquivo.find("perdida(s)");
    while (p != std::string::npos) {
        ++avisos;
        p = b.cartao.arquivo.find("perdida(s)", p + 1);
    }
    EXPECT_EQ(avisos, 1U) << "repetiu o aviso a cada descarga";
}

// ============================================================ reentrância

TEST(LoggerCartao, o_cartao_logando_a_propria_falha_nao_recursiona) {
    // O CartaoSd registra as proprias falhas. Sem a guarda, gravar uma falha
    // de gravacao tentaria gravar a mensagem sobre a falha de gravacao.
    Bancada b;
    b.cartao.realimenta = &b.log;   // cada acréscimo dispara um error()
    b.log.warning("teste", "dispara");
    SUCCEED() << "nao entrou em recursao infinita";
    EXPECT_GE(b.console.entradas().size(), 2U);
}

TEST(LoggerCartao, descarregar_de_dentro_de_uma_descarga_nao_recursiona) {
    // `descarrega()` e publica e o `main` a chama. Se algo a acionar enquanto
    // uma gravacao esta em curso, o guarda e a unica coisa entre isso e a
    // recursao -- aqui ele e a UNICA protecao no caminho, porque a
    // reentrancia nao passa por `registra()`.
    Bancada b;
    b.cartao.redescarrega = &b.log;
    b.log.warning("teste", "dispara");
    EXPECT_EQ(b.cartao.escritas.size(), 1U)
        << "a descarga reentrante gravou o mesmo buffer mais de uma vez";
}

TEST(LoggerCartao, a_mensagem_do_cartao_vai_ao_console_e_nao_ao_buffer) {
    // O logger passado ao cartao e o SEGUINTE, nao `*this`: senao as
    // mensagens do cartao realimentariam o proprio buffer que as gerou.
    Bancada b;
    b.log.warning("teste", "linha");
    const std::size_t antes = b.cartao.arquivo.size();
    b.log.descarrega();
    EXPECT_EQ(b.cartao.arquivo.size(), antes) << "realimentou o buffer";
}

// ============================================================== formato

TEST(LoggerCartao, a_linha_traz_nivel_origem_e_mensagem) {
    Bancada b;
    b.log.error("rede", "sem enlace");
    EXPECT_NE(b.cartao.arquivo.find("rede"), std::string::npos);
    EXPECT_NE(b.cartao.arquivo.find("sem enlace"), std::string::npos);
    EXPECT_EQ(b.cartao.arquivo.back(), '\n');
}


// --- o desvio durante a remessa ------------------------------------------

TEST(LoggerCartao, usa_arquivo_desvia_as_linhas_seguintes) {
    Bancada b;
    b.log.usa_arquivo("remessa.log");
    b.log.info("remessa", "conectando");
    b.log.descarrega();

    EXPECT_NE(b.cartao.por_nome["remessa.log"].find("conectando"),
              std::string::npos);
    EXPECT_EQ(b.cartao.por_nome["coruja.log"].find("conectando"),
              std::string::npos);
}

TEST(LoggerCartao, usa_arquivo_descarrega_o_pendente_no_arquivo_ANTIGO) {
    // A parte que erra em silêncio: trocar sem descarregar faria as linhas já
    // acumuladas saírem no arquivo NOVO, e o `coruja.log` perderia o que
    // aconteceu antes da remessa -- justamente o contexto de que se precisa
    // para entender a remessa.
    Bancada b;
    b.log.info("app", "linha anterior");
    b.log.usa_arquivo("remessa.log");

    EXPECT_NE(b.cartao.por_nome["coruja.log"].find("linha anterior"),
              std::string::npos)
        << "o pendente nao foi descarregado antes da troca";
    EXPECT_EQ(b.cartao.por_nome["remessa.log"].find("linha anterior"),
              std::string::npos);
}

TEST(LoggerCartao, usa_arquivo_volta_para_o_original) {
    Bancada b;
    b.log.usa_arquivo("remessa.log");
    b.log.info("remessa", "durante");
    b.log.usa_arquivo("coruja.log");
    b.log.info("app", "depois");
    b.log.descarrega();

    EXPECT_NE(b.cartao.por_nome["remessa.log"].find("durante"),
              std::string::npos);
    EXPECT_NE(b.cartao.por_nome["coruja.log"].find("depois"),
              std::string::npos);
    EXPECT_EQ(b.cartao.por_nome["coruja.log"].find("durante"),
              std::string::npos);
}

TEST(LoggerCartao, usa_arquivo_com_nome_vazio_nao_muda_nada) {
    // Trocar para "" mandaria o log para um arquivo sem nome, e o que se
    // perderia seria exatamente o diagnóstico que o desvio existe para dar.
    Bancada b;
    b.log.usa_arquivo("");
    b.log.usa_arquivo(nullptr);
    b.log.info("app", "segue valendo");
    b.log.descarrega();
    EXPECT_NE(b.cartao.por_nome["coruja.log"].find("segue valendo"),
              std::string::npos);
}

}  // namespace
