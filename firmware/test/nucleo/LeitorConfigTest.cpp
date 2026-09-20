#include "nucleo/LeitorConfig.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "apoio/LoggerMock.h"

namespace {

using namespace coruja;

ResultadoConfig le(const std::string& s, Logger* log = nullptr) {
    return le_config(s.data(), s.size(), log);
}

// --- caminho normal ---

TEST(LeitorConfig, LeRedesEUrls) {
    const auto r = le(
        "wifi_ssid_1=casa\n"
        "wifi_senha_1=segredo\n"
        "wifi_ssid_2=celular\n"
        "wifi_senha_2=outra\n"
        "url_versao=https://ex/v.txt\n"
        "url_base=https://ex/radares.bin\n");

    ASSERT_EQ(r.config.n_redes, 2U);
    EXPECT_STREQ(r.config.redes[0].ssid, "casa");
    EXPECT_STREQ(r.config.redes[1].ssid, "celular");
    EXPECT_STREQ(r.config.url_versao, "https://ex/v.txt");
    EXPECT_STREQ(r.config.url_base, "https://ex/radares.bin");
    EXPECT_TRUE(r.config.ota_possivel());
    EXPECT_TRUE(r.diagnostico.limpo());
}

TEST(LeitorConfig, APrioridadeEAOrdemDoArquivo) {
    // A escolha é por ordem, não por sinal: previsível e depurável.
    const auto r = le(
        "wifi_ssid_1=primeira\nwifi_senha_1=a\n"
        "wifi_ssid_2=segunda\nwifi_senha_2=b\n"
        "wifi_ssid_3=terceira\nwifi_senha_3=c\n");
    ASSERT_EQ(r.config.n_redes, 3U);
    EXPECT_STREQ(r.config.redes[0].ssid, "primeira");
    EXPECT_STREQ(r.config.redes[2].ssid, "terceira");
}

TEST(LeitorConfig, IgnoraComentariosELinhasEmBranco) {
    const auto r = le(
        "# um comentario\n"
        "\n"
        "   \n"
        "  # comentario indentado\n"
        "wifi_ssid_1=casa\n");
    EXPECT_EQ(r.config.n_redes, 1U);
    EXPECT_EQ(r.diagnostico.linhas_lidas, 1U);
    EXPECT_TRUE(r.diagnostico.limpo());
}

TEST(LeitorConfig, AparaEspacosEmVoltaDeChaveEValor) {
    const auto r = le("  wifi_ssid_1  =   casa   \n");
    EXPECT_STREQ(r.config.redes[0].ssid, "casa");
}

TEST(LeitorConfig, AceitaUltimaLinhaSemQuebra) {
    const auto r = le("url_base=https://ex/b.bin");
    EXPECT_STREQ(r.config.url_base, "https://ex/b.bin");
}

TEST(LeitorConfig, AceitaTerminadorDeLinhaDoWindows) {
    // O arquivo pode ser editado no cartão por qualquer sistema.
    const auto r = le("wifi_ssid_1=casa\r\nurl_base=https://x\r\n");
    EXPECT_STREQ(r.config.redes[0].ssid, "casa");
    EXPECT_STREQ(r.config.url_base, "https://x");
}

// --- o detalhe que motivou o formato numerado ---

TEST(LeitorConfig, SenhaPodeConterIgual) {
    // É a razão de dividir no PRIMEIRO '=' e de não usar `ssid:senha`.
    const auto r = le("wifi_ssid_1=casa\nwifi_senha_1=a=b=c\n");
    EXPECT_STREQ(r.config.redes[0].senha, "a=b=c");
}

TEST(LeitorConfig, SenhaPodeConterEspacosInternos) {
    const auto r = le("wifi_ssid_1=casa\nwifi_senha_1=duas palavras\n");
    EXPECT_STREQ(r.config.redes[0].senha, "duas palavras");
}

TEST(LeitorConfig, SsidPodeConterEspacos) {
    const auto r = le("wifi_ssid_1=rede da casa\n");
    EXPECT_STREQ(r.config.redes[0].ssid, "rede da casa");
}

// --- tolerância: nada aqui pode impedir o firmware de operar ---

TEST(LeitorConfig, ArquivoAusenteDaConfigVaziaSemQuebrar) {
    teste::LoggerMock log;
    const auto r = le_config(nullptr, 0, &log);
    EXPECT_EQ(r.config.n_redes, 0U);
    EXPECT_FALSE(r.config.ota_possivel());
    EXPECT_GE(log.contagem(Nivel::Warning), 1U);
}

TEST(LeitorConfig, LinhaSemIgualEContadaEIgnorada) {
    const auto r = le("isto nao e uma atribuicao\nwifi_ssid_1=casa\n");
    EXPECT_EQ(r.diagnostico.linhas_sem_igual, 1U);
    EXPECT_EQ(r.config.n_redes, 1U);
}

TEST(LeitorConfig, ChaveDesconhecidaEContadaEIgnorada) {
    // Chaves que já existiram e saíram: brilho, fuso, inversão do encoder.
    const auto r = le("brilho_inicial=40\nfuso_utc=-3\nwifi_ssid_1=casa\n");
    EXPECT_EQ(r.diagnostico.chaves_desconhecidas, 2U);
    EXPECT_EQ(r.config.n_redes, 1U);
}

TEST(LeitorConfig, ValorLongoDemaisERejeitadoEmVezDeTruncado) {
    // Truncar uma URL produz um valor que PARECE válido e falha em campo com
    // sintoma obscuro. Rejeitar produz um aviso no boot.
    const std::string longa(kMaxUrl + 1, 'x');
    const auto r = le("url_base=https://" + longa + "\n");
    EXPECT_EQ(r.diagnostico.valores_longos, 1U);
    EXPECT_STREQ(r.config.url_base, "") << "truncou em vez de rejeitar";
}

TEST(LeitorConfig, SsidNoLimiteExatoEAceita) {
    const std::string ssid(kMaxSsid, 'a');
    const auto r = le("wifi_ssid_1=" + ssid + "\n");
    EXPECT_EQ(r.diagnostico.valores_longos, 0U);
    EXPECT_EQ(std::strlen(r.config.redes[0].ssid), kMaxSsid);
}

TEST(LeitorConfig, IndiceAcimaDoTetoEContadoEIgnorado) {
    const auto r = le("wifi_ssid_9=demais\nwifi_ssid_1=casa\n");
    EXPECT_EQ(r.diagnostico.indices_fora, 1U);
    EXPECT_EQ(r.config.n_redes, 1U);
    EXPECT_STREQ(r.config.redes[0].ssid, "casa");
}

TEST(LeitorConfig, SenhaSemSsidEContadaComoIncompleta) {
    const auto r = le("wifi_senha_2=orfa\nwifi_ssid_1=casa\n");
    EXPECT_EQ(r.diagnostico.redes_incompletas, 1U);
    EXPECT_EQ(r.config.n_redes, 1U);
}

TEST(LeitorConfig, IndicesSalteadosSaoCompactadosEmOrdem) {
    // Recusar isto puniria um erro de digitação sem consequência.
    const auto r = le("wifi_ssid_1=um\nwifi_ssid_3=tres\n");
    ASSERT_EQ(r.config.n_redes, 2U);
    EXPECT_STREQ(r.config.redes[0].ssid, "um");
    EXPECT_STREQ(r.config.redes[1].ssid, "tres");
}

TEST(LeitorConfig, RedeAbertaSemSenhaEValida) {
    const auto r = le("wifi_ssid_1=aberta\n");
    ASSERT_EQ(r.config.n_redes, 1U);
    EXPECT_TRUE(r.config.redes[0].aberta());
    EXPECT_FALSE(r.config.redes[0].vazia());
}

TEST(LeitorConfig, ChaveRepetidaVenceAUltima) {
    const auto r = le("url_base=primeira\nurl_base=segunda\n");
    EXPECT_STREQ(r.config.url_base, "segunda");
}

// --- OTA só é possível com rede E URLs ---

TEST(LeitorConfig, SemUrlNaoHaOta) {
    teste::LoggerMock log;
    const auto r = le("wifi_ssid_1=casa\nwifi_senha_1=x\n", &log);
    EXPECT_TRUE(r.config.tem_rede());
    EXPECT_FALSE(r.config.tem_urls());
    EXPECT_FALSE(r.config.ota_possivel());
    EXPECT_TRUE(log.contem(Nivel::Warning, "OTA indisponivel"));
}

TEST(LeitorConfig, SemRedeNaoHaOta) {
    const auto r = le("url_versao=https://a\nurl_base=https://b\n");
    EXPECT_TRUE(r.config.tem_urls());
    EXPECT_FALSE(r.config.ota_possivel());
}

TEST(LeitorConfig, UmaSoUrlNaoBasta) {
    const auto r = le("wifi_ssid_1=c\nurl_base=https://b\n");
    EXPECT_FALSE(r.config.ota_possivel()) << "url_versao faltando passou";
}

// --- segredo ---

TEST(LeitorConfig, ASenhaNuncaAparaceNoLog) {
    // Requisito de segurança, não de estilo: o log sai pelo USB-CDC e pode ser
    // gravado por quem estiver depurando. Vale também para o caso de erro.
    const std::string segredo = "S3nh4-MuitoSecreta-1234";
    teste::LoggerMock log;
    le("wifi_ssid_1=casa\n"
       "wifi_senha_1=" + segredo + "\n"
       "wifi_senha_2=" + std::string(kMaxSenha + 5, 'z') + "\n"
       "url_base=https://ex\n", &log);

    for (const auto& e : log.entradas()) {
        EXPECT_EQ(e.mensagem.find(segredo), std::string::npos)
            << "a senha vazou no log: " << e.mensagem;
        EXPECT_EQ(e.mensagem.find("zzzz"), std::string::npos)
            << "a senha longa vazou ao ser rejeitada: " << e.mensagem;
    }
}

TEST(LeitorConfig, OLogRelataAContagemMasNaoOConteudo) {
    teste::LoggerMock log;
    le("wifi_ssid_1=casa\nwifi_senha_1=abc\nurl_versao=a\nurl_base=b\n", &log);
    EXPECT_TRUE(log.contem(Nivel::Info, "1 rede"));
    for (const auto& e : log.entradas()) {
        EXPECT_EQ(e.mensagem.find("casa"), std::string::npos)
            << "o SSID vazou; nao e segredo, mas o log nao precisa dele";
    }
}

// --- concordância entre o gerador em Python e o leitor em C++ ---

/// Lê o `coruja.cfg.exemplo` versionado, que é produzido pelo
/// `scripts/gera_config.py`.
///
/// Gerador e leitor são escritos em linguagens diferentes e evoluem em lados
/// diferentes do projeto. Este teste é o único ponto em que os dois se
/// encontram: se o gerador mudar de formato e ninguém avisar, ele falha.
std::string le_exemplo() {
    std::FILE* f = std::fopen(CORUJA_CFG_EXEMPLO, "rb");
    if (f == nullptr) {
        return {};
    }
    std::string s;
    char buf[512];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        s.append(buf, n);
    }
    std::fclose(f);
    return s;
}

TEST(LeitorConfigExemplo, OArquivoDoGeradorEAceito) {
    const auto texto = le_exemplo();
    ASSERT_FALSE(texto.empty()) << "nao achei " << CORUJA_CFG_EXEMPLO;

    teste::LoggerMock log;
    const auto r = le_config(texto.data(), texto.size(), &log);

    EXPECT_TRUE(r.diagnostico.limpo())
        << "o gerador produziu algo que o leitor nao entende: "
        << r.diagnostico.linhas_sem_igual << " sem '=', "
        << r.diagnostico.chaves_desconhecidas << " desconhecida(s), "
        << r.diagnostico.valores_longos << " longa(s)";
    EXPECT_EQ(r.config.n_redes, 2U) << "o exemplo deveria trazer duas redes";
    EXPECT_TRUE(r.config.tem_urls());
    EXPECT_TRUE(r.config.ota_possivel());
}

TEST(LeitorConfigExemplo, OExemploNaoCarregaSegredo) {
    // Ele é versionado. Se algum dia sair dali um SSID ou senha de verdade,
    // este teste é a última barreira antes do commit.
    const auto texto = le_exemplo();
    ASSERT_FALSE(texto.empty());
    const auto r = le_config(texto.data(), texto.size());
    for (std::size_t i = 0; i < r.config.n_redes; ++i) {
        EXPECT_STREQ(r.config.redes[i].ssid, "troque-me")
            << "SSID real vazou para o exemplo versionado";
        EXPECT_STREQ(r.config.redes[i].senha, "troque-me")
            << "senha real vazou para o exemplo versionado";
    }
    EXPECT_NE(std::string(r.config.url_base).find("exemplo"), std::string::npos)
        << "URL real vazou para o exemplo versionado";
}

}  // namespace
