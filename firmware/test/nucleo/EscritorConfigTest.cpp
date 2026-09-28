#include "nucleo/EscritorConfig.h"

#include <gtest/gtest.h>

#include <string>

#include "nucleo/LeitorConfig.h"

namespace {

using namespace coruja;

std::string reescreve(const std::string& origem, const Configuracao& cfg,
                      std::size_t folga = 512) {
    std::string destino(origem.size() + folga, '\0');
    const std::size_t n = reescreve_ajustes(origem.data(), origem.size(), cfg,
                                            destino.data(), destino.size());
    EXPECT_GT(n, 0U) << "nao coube";
    destino.resize(n);
    return destino;
}

Configuracao ajustada() {
    Configuracao c;
    std::snprintf(c.nome, sizeof(c.nome), "%s", "fusca");
    c.brilho_dia = 75;
    c.brilho_noite = 15;
    c.modo_noturno = ModoNoturno::SempreNoite;
    c.volume_buzzer = 50;
    return c;
}

// --- o que nao pode mudar ---

TEST(EscritorConfig, PreservaComentariosEOResto) {
    const std::string origem =
        "# Configuracao do Coruja GPS.\n"
        "#\n"
        "# Um comentario que fala de brilho_dia=100 sem ser a chave.\n"
        "wifi_ssid_1=casa\n"
        "wifi_senha_1=uma=senha=com=iguais\n"
        "url_base=https://ex/r.bin\n"
        "brilho_dia=100\n";
    const std::string saida = reescreve(origem, ajustada());

    EXPECT_NE(saida.find("# Configuracao do Coruja GPS.\n#\n"), std::string::npos);
    EXPECT_NE(saida.find("# Um comentario que fala de brilho_dia=100 sem ser a chave.\n"),
              std::string::npos)
        << "comentario com cara de chave foi reescrito";
    EXPECT_NE(saida.find("wifi_senha_1=uma=senha=com=iguais\n"), std::string::npos)
        << "senha com '=' foi mutilada";
    EXPECT_NE(saida.find("\nbrilho_dia=75\n"), std::string::npos);
    EXPECT_EQ(saida.find("\nbrilho_dia=100"), std::string::npos)
        << "a chave nao foi atualizada";
    // O 'brilho_dia=100' que resta e o de dentro do comentario, acima.
}

TEST(EscritorConfig, PreservaEspacosEmVoltaDoIgual) {
    // A indentacao e os espacos sao de quem editou o arquivo.
    const std::string origem =
        "nome=x\n   brilho_dia  =  100  \nbrilho_noite=20\n"
        "modo_noturno=auto\nvolume_buzzer=100\n";
    const std::string saida = reescreve(origem, ajustada());
    EXPECT_EQ(saida,
              "nome=fusca\n   brilho_dia  =  75\nbrilho_noite=15\n"
              "modo_noturno=noite\nvolume_buzzer=50\n")
        << "os espacos em volta do '=' sao de quem editou o arquivo";
}

TEST(EscritorConfig, PreservaFimDeLinhaDoWindows) {
    const std::string origem =
        "nome=x\r\nbrilho_dia=100\r\nbrilho_noite=20\r\n"
        "modo_noturno=auto\r\nvolume_buzzer=100\r\n";
    const std::string saida = reescreve(origem, ajustada());
    EXPECT_EQ(saida,
              "nome=fusca\r\nbrilho_dia=75\r\nbrilho_noite=15\r\n"
              "modo_noturno=noite\r\nvolume_buzzer=50\r\n")
        << "finais de linha ficaram misturados";
}

TEST(EscritorConfig, GravarOMesmoValorNaoMudaNadaNoArquivo) {
    // Idempotencia: abrir o menu e sair sem mexer nao pode sujar o cartao
    // nem gastar um ciclo de escrita por nada.
    const std::string origem =
        "nome=fusca\nbrilho_dia=75\nbrilho_noite=15\n"
        "modo_noturno=noite\nvolume_buzzer=50\n";
    EXPECT_EQ(reescreve(origem, ajustada()), origem);
}

TEST(EscritorConfig, DuasPassadasDaoOMesmoResultado) {
    const std::string origem = "url_base=https://ex/r.bin\n";
    const std::string uma = reescreve(origem, ajustada());
    EXPECT_EQ(reescreve(uma, ajustada()), uma);
}

// --- o que tem de mudar ---

TEST(EscritorConfig, ReescreveTodasAsOcorrenciasDaChaveRepetida) {
    // O leitor honra a ultima. Deixar a primeira para tras faria o
    // arquivo mostrar dois valores para o mesmo ajuste.
    const std::string origem =
        "volume_buzzer=100\nnome=x\nbrilho_dia=100\nbrilho_noite=20\n"
        "modo_noturno=auto\nvolume_buzzer=100\n";
    const std::string saida = reescreve(origem, ajustada());
    EXPECT_EQ(saida.find("volume_buzzer=100"), std::string::npos)
        << "sobrou uma ocorrencia com o valor velho";
    EXPECT_EQ(saida,
              "volume_buzzer=50\nnome=fusca\nbrilho_dia=75\nbrilho_noite=15\n"
              "modo_noturno=noite\nvolume_buzzer=50\n");
}

TEST(EscritorConfig, AcrescentaAsChavesAusentes) {
    const std::string origem = "url_base=https://ex/r.bin\n";
    const std::string saida = reescreve(origem, ajustada());
    EXPECT_EQ(saida.find("url_base=https://ex/r.bin\n"), 0U)
        << "o que ja estava la saiu do lugar";
    for (const char* esperado : {"nome=fusca", "brilho_dia=75",
                                 "brilho_noite=15", "modo_noturno=noite",
                                 "volume_buzzer=50"}) {
        EXPECT_NE(saida.find(esperado), std::string::npos) << esperado;
    }
}

TEST(EscritorConfig, ArquivoSemNovaLinhaNoFimNaoGrudaAChave) {
    const std::string saida = reescreve("url_base=https://ex/r.bin", ajustada());
    EXPECT_EQ(saida.find("https://ex/r.binnome"), std::string::npos);
    EXPECT_NE(saida.find("r.bin\n"), std::string::npos);
}

TEST(EscritorConfig, ArquivoVazioSaiSoComOsAjustes) {
    const std::string saida = reescreve("", ajustada());
    const auto r = le_config(saida.data(), saida.size());
    EXPECT_STREQ(r.config.nome, "fusca");
    EXPECT_EQ(r.config.volume_buzzer, 50);
    EXPECT_TRUE(r.diagnostico.limpo());
}

// --- o acordo com o leitor ---

TEST(EscritorConfig, OQueFoiEscritoEOQueOLeitorLe) {
    // O teste que mais importa: escritor e leitor concordando sobre o
    // mesmo arquivo. Sem isto o ajuste aparece salvo e nao vale.
    const std::string origem =
        "# comentario\n"
        "wifi_ssid_1=casa\n"
        "wifi_senha_1=segredo\n"
        "url_versao=https://ex/v\n"
        "url_base=https://ex/r.bin\n"
        "brilho_dia=100\n";
    const std::string saida = reescreve(origem, ajustada());
    const auto r = le_config(saida.data(), saida.size());

    EXPECT_STREQ(r.config.nome, "fusca");
    EXPECT_EQ(r.config.brilho_dia, 75);
    EXPECT_EQ(r.config.brilho_noite, 15);
    EXPECT_EQ(r.config.modo_noturno, ModoNoturno::SempreNoite);
    EXPECT_EQ(r.config.volume_buzzer, 50);
    // E o que nao era nosso continua inteiro.
    ASSERT_EQ(r.config.n_redes, 1U);
    EXPECT_STREQ(r.config.redes[0].ssid, "casa");
    EXPECT_STREQ(r.config.redes[0].senha, "segredo");
    EXPECT_STREQ(r.config.url_base, "https://ex/r.bin");
    EXPECT_TRUE(r.diagnostico.limpo());
}

TEST(EscritorConfig, OExemploVersionadoSobreviveAUmaGravacao) {
    // O arquivo de verdade, com todos os comentarios: so as cinco linhas
    // de ajuste podem mudar.
    std::FILE* f = std::fopen(CORUJA_CFG_EXEMPLO, "rb");
    ASSERT_NE(f, nullptr);
    std::string origem;
    char buf[1024];
    std::size_t lidos = 0;
    while ((lidos = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        origem.append(buf, lidos);
    }
    std::fclose(f);
    ASSERT_FALSE(origem.empty());

    const std::string saida = reescreve(origem, ajustada());
    std::size_t diferentes = 0;
    std::size_t a = 0;
    std::size_t b = 0;
    while (a < origem.size() || b < saida.size()) {
        const std::size_t fa = origem.find('\n', a);
        const std::size_t fb = saida.find('\n', b);
        const std::string la = origem.substr(a, fa - a);
        const std::string lb = saida.substr(b, fb - b);
        if (la != lb) {
            ++diferentes;
        }
        if (fa == std::string::npos || fb == std::string::npos) {
            break;
        }
        a = fa + 1;
        b = fb + 1;
    }
    EXPECT_EQ(diferentes, 5U) << "mudou linha que nao era ajuste";
}

// --- capacidade ---

TEST(EscritorConfig, CapacidadeExataCabe) {
    const std::string origem = "brilho_dia=100\n";
    char destino[512];
    const std::size_t n = reescreve_ajustes(origem.data(), origem.size(),
                                            ajustada(), destino, sizeof(destino));
    ASSERT_GT(n, 0U);
    char justo[512];
    EXPECT_EQ(reescreve_ajustes(origem.data(), origem.size(), ajustada(),
                                justo, n), n);
}

TEST(EscritorConfig, UmByteAMenosRecusaEmVezDeTruncar) {
    // Truncar deixaria no cartao um arquivo pela metade, e o proximo boot
    // leria uma configuracao mutilada sem nenhum aviso.
    const std::string origem = "brilho_dia=100\n";
    char destino[512];
    const std::size_t n = reescreve_ajustes(origem.data(), origem.size(),
                                            ajustada(), destino, sizeof(destino));
    ASSERT_GT(n, 0U);
    EXPECT_EQ(reescreve_ajustes(origem.data(), origem.size(), ajustada(),
                                destino, n - 1), 0U);
}

TEST(EscritorConfig, CapacidadeZeroRecusa) {
    char destino[1];
    EXPECT_EQ(reescreve_ajustes("brilho_dia=100\n", 15, ajustada(), destino, 0),
              0U);
}

}  // namespace
