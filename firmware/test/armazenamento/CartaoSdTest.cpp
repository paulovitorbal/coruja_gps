#include "armazenamento/CartaoSd.h"

#include <gtest/gtest.h>

#include <string>

#include "apoio/LoggerMock.h"
#include "apoio/fatfs/FatFsFalso.h"

namespace {

using namespace coruja;
using coruja::teste::FatFsFalso;

struct CartaoSdNoHost : public ::testing::Test {
    teste::LoggerMock log;
    CartaoSd          sd;
    FatFsFalso&       fs = FatFsFalso::instancia();

    void SetUp() override { fs.reinicia(); }

    void poe(int volume, const std::string& nome, const std::string& conteudo) {
        fs.volumes[volume].monta = true;
        fs.volumes[volume].arquivos[nome] = conteudo;
    }
    std::string le(const char* nome) {
        char buf[256] = {};
        std::size_t n = 0;
        sd.le_arquivo(nome, buf, sizeof buf, &n, log);
        return std::string(buf, n);
    }
};

// ======================================== R-38: sondagem de partições

TEST_F(CartaoSdNoHost, acha_o_arquivo_na_primeira_particao) {
    poe(0, "coruja.cfg", "conteudo");
    EXPECT_EQ(le("coruja.cfg"), "conteudo");
}

TEST_F(CartaoSdNoHost, acha_o_arquivo_numa_particao_ADIANTE_da_primeira_FAT) {
    // O R-38 em uma frase: a primeira particao monta e NAO tem o arquivo.
    // Escolher pela primeira FAT daria "sem configuracao" com o arquivo no
    // cartao -- e foi o que acontecia antes de FF_MULTI_PARTITION=1.
    fs.volumes[0].monta = true;                 // monta, vazia
    poe(2, "coruja.cfg", "achado na 2");
    EXPECT_EQ(le("coruja.cfg"), "achado na 2");
}

TEST_F(CartaoSdNoHost, procura_ate_a_ultima_particao) {
    fs.volumes[0].monta = true;
    fs.volumes[1].monta = true;
    fs.volumes[2].monta = true;
    fs.volumes[3].monta = true;
    poe(4, "coruja.cfg", "na ultima");
    EXPECT_EQ(le("coruja.cfg"), "na ultima");
}

TEST_F(CartaoSdNoHost, pula_as_que_nao_montam_e_acha_na_seguinte) {
    fs.volumes[0].monta = false;
    fs.volumes[0].erro_montagem = FR_NO_FILESYSTEM;
    fs.volumes[1].monta = false;
    fs.volumes[1].erro_montagem = FR_DISK_ERR;
    poe(2, "coruja.cfg", "resistiu");
    EXPECT_EQ(le("coruja.cfg"), "resistiu");
}

TEST_F(CartaoSdNoHost, desmonta_as_particoes_que_sondou) {
    // Deixar volume montado consome estrutura do FatFs e, em cartao lento,
    // segura o barramento.
    fs.volumes[0].monta = true;
    poe(1, "coruja.cfg", "x");
    le("coruja.cfg");
    EXPECT_EQ(fs.montagens, fs.desmontagens)
        << "montou mais do que desmontou";
}

// ================================ ADR 0010: ausente e ilegivel sao um caso

TEST_F(CartaoSdNoHost, nenhuma_particao_monta_e_cartao_ilegivel) {
    char buf[64];
    std::size_t n = 0;
    EXPECT_EQ(sd.le_arquivo("coruja.cfg", buf, sizeof buf, &n, log),
              ErroCartao::SemCartaoLegivel);
}

TEST_F(CartaoSdNoHost, montou_mas_sem_o_arquivo_e_ausencia_e_nao_ilegibilidade) {
    // Sao dois diagnosticos com duas solucoes: formatar o cartao contra
    // copiar o arquivo. Confundi-los manda o dono fazer a coisa errada.
    fs.volumes[0].monta = true;
    fs.volumes[1].monta = true;
    char buf[64];
    std::size_t n = 0;
    EXPECT_EQ(sd.le_arquivo("coruja.cfg", buf, sizeof buf, &n, log),
              ErroCartao::ArquivoAusente);
}

TEST_F(CartaoSdNoHost, arquivo_ausente_nao_e_registrado_como_erro) {
    // Na primeira atualizacao o versao.txt nao existe, e isso e normal. Quem
    // sabe se o arquivo era obrigatorio e quem chamou.
    fs.volumes[0].monta = true;
    char buf[64];
    std::size_t n = 0;
    sd.le_arquivo("versao.txt", buf, sizeof buf, &n, log);
    EXPECT_EQ(log.contagem(Nivel::Error), 0U);
}

TEST_F(CartaoSdNoHost, arquivo_maior_que_o_buffer_e_recusado_sem_estourar) {
    poe(0, "grande.txt", std::string(500, 'x'));
    char buf[64];
    std::size_t n = 0;
    EXPECT_EQ(sd.le_arquivo("grande.txt", buf, sizeof buf, &n, log),
              ErroCartao::ArquivoGrande);
    EXPECT_EQ(n, 0U);
}

TEST_F(CartaoSdNoHost, driver_que_nao_inicia_nao_derruba_a_leitura) {
    fs.driver_inicia = false;
    poe(0, "coruja.cfg", "x");
    char buf[64];
    std::size_t n = 0;
    sd.le_arquivo("coruja.cfg", buf, sizeof buf, &n, log);
    SUCCEED() << "nao travou nem estourou";
}

// ================================================ RF05.2: troca atômica

TEST_F(CartaoSdNoHost, promove_guarda_a_base_anterior_como_reserva) {
    poe(0, "radares.bin", "base velha");
    fs.volumes[0].arquivos["radares.tmp"] = "base nova";
    ASSERT_EQ(sd.promove("radares.tmp", "radares.bin", "radares.bak", log),
              ErroCartao::Nenhum);
    EXPECT_EQ(fs.conteudo(0, "radares.bin"), "base nova");
    EXPECT_EQ(fs.conteudo(0, "radares.bak"), "base velha");
    EXPECT_FALSE(fs.existe(0, "radares.tmp"));
}

TEST_F(CartaoSdNoHost, na_primeira_atualizacao_nao_ha_base_a_guardar) {
    // Ausencia de base nao e erro: e o estado de um cartao novo.
    fs.volumes[0].monta = true;
    fs.volumes[0].arquivos["radares.tmp"] = "primeira";
    EXPECT_EQ(sd.promove("radares.tmp", "radares.bin", "radares.bak", log),
              ErroCartao::Nenhum);
    EXPECT_EQ(fs.conteudo(0, "radares.bin"), "primeira");
    EXPECT_FALSE(fs.existe(0, "radares.bak"));
}

TEST_F(CartaoSdNoHost, uma_reserva_antiga_nao_impede_a_troca) {
    // Sem apagar o .bak anterior, o rename falharia com FR_EXIST e a
    // atualizacao morreria na segunda vez em diante.
    poe(0, "radares.bin", "base 2");
    fs.volumes[0].arquivos["radares.bak"] = "base 1";
    fs.volumes[0].arquivos["radares.tmp"] = "base 3";
    ASSERT_EQ(sd.promove("radares.tmp", "radares.bin", "radares.bak", log),
              ErroCartao::Nenhum);
    EXPECT_EQ(fs.conteudo(0, "radares.bin"), "base 3");
    EXPECT_EQ(fs.conteudo(0, "radares.bak"), "base 2");
}

TEST_F(CartaoSdNoHost, renomeacao_que_falha_vira_erro_de_renomeacao) {
    poe(0, "radares.bin", "base velha");
    fs.volumes[0].arquivos["radares.tmp"] = "base nova";
    fs.erro_rename = FR_DISK_ERR;
    EXPECT_EQ(sd.promove("radares.tmp", "radares.bin", "radares.bak", log),
              ErroCartao::FalhaDeRenomeacao);
    EXPECT_EQ(fs.conteudo(0, "radares.bin"), "base velha")
        << "a base vigente tinha de ficar intacta";
}

TEST_F(CartaoSdNoHost, promover_sem_cartao_legivel) {
    EXPECT_EQ(sd.promove("radares.tmp", "radares.bin", "radares.bak", log),
              ErroCartao::SemCartaoLegivel);
}

// ================================================= escrita e acréscimo

TEST_F(CartaoSdNoHost, escrita_em_fluxo_grava_o_que_recebeu) {
    fs.volumes[0].monta = true;
    ASSERT_EQ(sd.abre_para_escrita("radares.tmp", log), ErroCartao::Nenhum);
    const std::uint8_t a[] = {'a', 'b'};
    const std::uint8_t b[] = {'c'};
    EXPECT_TRUE(sd.escreve(a, sizeof a));
    EXPECT_TRUE(sd.escreve(b, sizeof b));
    ASSERT_EQ(sd.conclui_escrita(log), ErroCartao::Nenhum);
    EXPECT_EQ(fs.conteudo(0, "radares.tmp"), "abc");
}

TEST_F(CartaoSdNoHost, cartao_que_enche_no_meio_faz_escreve_devolver_falso) {
    fs.volumes[0].monta = true;
    fs.escrita_falha_apos = 2;
    ASSERT_EQ(sd.abre_para_escrita("radares.tmp", log), ErroCartao::Nenhum);
    const std::uint8_t a[] = {'a', 'b'};
    EXPECT_TRUE(sd.escreve(a, sizeof a));
    EXPECT_FALSE(sd.escreve(a, sizeof a));
}

TEST_F(CartaoSdNoHost, descartar_a_escrita_nao_deixa_o_temporario) {
    fs.volumes[0].monta = true;
    ASSERT_EQ(sd.abre_para_escrita("radares.tmp", log), ErroCartao::Nenhum);
    const std::uint8_t a[] = {'x'};
    sd.escreve(a, sizeof a);
    sd.descarta_escrita("radares.tmp", log);
    EXPECT_FALSE(fs.existe(0, "radares.tmp"));
}

TEST_F(CartaoSdNoHost, acrescentar_nao_reescreve_o_arquivo_inteiro) {
    poe(0, "coruja.log", "primeira\n");
    ASSERT_EQ(sd.acrescenta_arquivo("coruja.log", "segunda\n", 8, log),
              ErroCartao::Nenhum);
    EXPECT_EQ(fs.conteudo(0, "coruja.log"), "primeira\nsegunda\n");
}

TEST_F(CartaoSdNoHost, gravar_substitui_o_conteudo) {
    poe(0, "versao.txt", "antiga");
    ASSERT_EQ(sd.grava_arquivo("versao.txt", "nova", 4, log),
              ErroCartao::Nenhum);
    EXPECT_EQ(fs.conteudo(0, "versao.txt"), "nova");
}

TEST_F(CartaoSdNoHost, a_escrita_vai_ao_MESMO_volume_onde_a_leitura_achou) {
    // A base tem de acompanhar a configuracao que a definiu. Se a escrita
    // sondasse de novo, poderia gravar numa particao diferente da lida.
    fs.volumes[0].monta = true;                  // monta, vazia
    poe(3, "coruja.cfg", "config aqui");
    ASSERT_EQ(le("coruja.cfg"), "config aqui");
    ASSERT_EQ(sd.grava_arquivo("versao.txt", "v1", 2, log), ErroCartao::Nenhum);
    EXPECT_TRUE(fs.existe(3, "versao.txt")) << "gravou na particao errada";
    EXPECT_FALSE(fs.existe(0, "versao.txt"));
}

}  // namespace
