#include "log/LoggerConsole.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "apoio/LoggerMock.h"
#include "log/IdExecucao.h"

namespace {

using namespace coruja;

std::string captura(Nivel minimo, void (*acao)(Logger&)) {
    std::FILE* f = std::tmpfile();
    LoggerConsole log(f, minimo);
    acao(log);
    std::rewind(f);
    std::string out;
    char buf[256];
    while (std::fgets(buf, sizeof buf, f) != nullptr) {
        out += buf;
    }
    std::fclose(f);
    return out;
}

TEST(LoggerConsole, EscreveNivelOrigemEMensagem) {
    const auto s = captura(Nivel::Debug, [](Logger& l) {
        l.info("gps", "fix adquirido");
    });
    EXPECT_NE(s.find("INFO"), std::string::npos);
    EXPECT_NE(s.find("gps"), std::string::npos);
    EXPECT_NE(s.find("fix adquirido"), std::string::npos);
}

TEST(LoggerConsole, DescartaAbaixoDoNivelMinimo) {
    const auto s = captura(Nivel::Warning, [](Logger& l) {
        l.debug("x", "nao deve aparecer");
        l.info("x", "nem esta");
        l.warning("x", "esta sim");
    });
    EXPECT_EQ(s.find("nao deve aparecer"), std::string::npos);
    EXPECT_EQ(s.find("nem esta"), std::string::npos);
    EXPECT_NE(s.find("esta sim"), std::string::npos);
}

TEST(LoggerConsole, NivelMinimoEAjustavelEmTempoDeExecucao) {
    const auto s = captura(Nivel::Error, [](Logger& l) {
        l.info("x", "antes");
        l.define_nivel_minimo(Nivel::Info);
        l.info("x", "depois");
    });
    EXPECT_EQ(s.find("antes"), std::string::npos);
    EXPECT_NE(s.find("depois"), std::string::npos);
}

TEST(LoggerConsole, AceitaPonteiroNuloSemQuebrar) {
    const auto s = captura(Nivel::Debug, [](Logger& l) {
        l.registra(Nivel::Error, nullptr, nullptr);
    });
    EXPECT_NE(s.find("ERRO"), std::string::npos);
}

TEST(LoggerConsole, TodosOsNiveisTemNome) {
    for (auto n : {Nivel::Debug, Nivel::Info, Nivel::Warning, Nivel::Error}) {
        EXPECT_STRNE(nome_nivel(n), "?");
    }
}

// --- o mock, que os outros testes usam (regra 2) ---

TEST(LoggerMock, GuardaOQueFoiRegistrado) {
    teste::LoggerMock log;
    log.info("a", "primeira");
    log.error("b", "segunda");
    ASSERT_EQ(log.entradas().size(), 2U);
    EXPECT_EQ(log.entradas()[0].origem, "a");
    EXPECT_EQ(log.entradas()[1].nivel, Nivel::Error);
    EXPECT_TRUE(log.contem(Nivel::Error, "segunda"));
    EXPECT_FALSE(log.contem(Nivel::Info, "segunda"));
    EXPECT_EQ(log.contagem(Nivel::Info), 1U);
}

TEST(LoggerMock, RespeitaNivelMinimo) {
    teste::LoggerMock log;
    log.define_nivel_minimo(Nivel::Warning);
    log.debug("x", "descartada");
    log.error("x", "guardada");
    EXPECT_EQ(log.entradas().size(), 1U);
}


// ================================ o identificador de execucao na linha

TEST(LoggerConsole, prefixa_as_linhas_com_o_identificador_da_execucao) {
    // O `coruja.log` acumula entre ligações, e sem isto só dá para inferir a
    // fronteira entre uma e outra pelo que aparece no boot.
    define_id_execucao(0xDEADBEEFCAFEBABEULL);
    const std::string id = id_execucao();
    ASSERT_EQ(id.size(), kTamIdExecucao);

    char buf[256] = {};
    FILE* f = fmemopen(buf, sizeof buf, "w");
    LoggerConsole log(f, Nivel::Debug);
    log.info("gps", "uma mensagem");
    std::fclose(f);

    const std::string linha = buf;
    EXPECT_EQ(linha.rfind(id, 0), 0U) << "o id tem de vir PRIMEIRO: " << linha;
    EXPECT_NE(linha.find("[INFO "), std::string::npos) << linha;
    EXPECT_NE(linha.find("gps: uma mensagem"), std::string::npos) << linha;
}

TEST(LoggerConsole, sem_identificador_a_linha_nao_comeca_com_espaco) {
    // Antes do boot sortear -- e no alvo de host, que nao sorteia -- a linha
    // tem de sair como sempre saiu. Espaco solto no comeco e sujeira que
    // ninguem conserta depois, porque ninguem sabe de onde veio.
    esquece_id_execucao();

    char buf[256] = {};
    FILE* f = fmemopen(buf, sizeof buf, "w");
    LoggerConsole log(f, Nivel::Debug);
    log.info("gps", "uma mensagem");
    std::fclose(f);

    EXPECT_EQ(std::string(buf).rfind("[INFO ", 0), 0U)
        << "esperava a linha comecando em '[': " << buf;
}

}  // namespace