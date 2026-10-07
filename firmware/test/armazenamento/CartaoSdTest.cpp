#include "armazenamento/CartaoSd.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

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


// ===========================================================================
// Arquivario -- o que a remessa de dados usa
// ===========================================================================

struct Colhidos {
    std::vector<std::pair<std::string, std::size_t>> itens;
    static void ao_listar(void* ctx, const char* nome, std::size_t tamanho) {
        static_cast<Colhidos*>(ctx)->itens.emplace_back(nome, tamanho);
    }
    bool tem(const std::string& nome) const {
        for (const auto& i : itens) { if (i.first == nome) { return true; } }
        return false;
    }
};

TEST_F(CartaoSdNoHost, lista_os_arquivos_com_o_tamanho) {
    poe(0, "coruja.log", "doze bytes!");
    poe(0, "infracoes.log", "x");
    Colhidos c;
    ASSERT_EQ(sd.lista(Colhidos::ao_listar, &c, log), ErroCartao::Nenhum);
    ASSERT_EQ(c.itens.size(), 2u);
    EXPECT_TRUE(c.tem("coruja.log"));
    EXPECT_TRUE(c.tem("infracoes.log"));
    for (const auto& i : c.itens) {
        if (i.first == "coruja.log") { EXPECT_EQ(i.second, 11u); }
    }
}

TEST_F(CartaoSdNoHost, a_listagem_ignora_diretorios) {
    // A remessa manda tudo que a listagem apresenta como arquivo. Um
    // diretório chamado "coruja.log" seria aberto, lido como vazio e
    // apagado.
    poe(0, "coruja.log", "dado");
    poe(0, "System Volume Information", "");
    fs.diretorios.insert("System Volume Information");
    Colhidos c;
    ASSERT_EQ(sd.lista(Colhidos::ao_listar, &c, log), ErroCartao::Nenhum);
    EXPECT_TRUE(c.tem("coruja.log"));
    EXPECT_FALSE(c.tem("System Volume Information"));
}

TEST_F(CartaoSdNoHost, cartao_vazio_lista_sem_erro) {
    fs.volumes[0].monta = true;
    Colhidos c;
    EXPECT_EQ(sd.lista(Colhidos::ao_listar, &c, log), ErroCartao::Nenhum);
    EXPECT_TRUE(c.itens.empty());
}

TEST_F(CartaoSdNoHost, sem_cartao_a_listagem_falha_e_nao_vem_vazia) {
    // "Não li" e "não havia nada" levam a conclusões opostas: a segunda faria
    // a remessa dizer que está tudo entregue com o cartão inteiro parado.
    Colhidos c;
    EXPECT_EQ(sd.lista(Colhidos::ao_listar, &c, log),
              ErroCartao::SemCartaoLegivel);
}

TEST_F(CartaoSdNoHost, falha_no_meio_da_enumeracao_e_erro) {
    poe(0, "coruja.log", "dado");
    fs.erro_readdir = FR_DISK_ERR;
    Colhidos c;
    EXPECT_EQ(sd.lista(Colhidos::ao_listar, &c, log), ErroCartao::FalhaDeLeitura);
}

TEST_F(CartaoSdNoHost, a_listagem_desmonta_no_fim) {
    // O cartão é removível; deixar o volume montado é como se corrompe um
    // sistema de arquivos.
    poe(0, "coruja.log", "dado");
    Colhidos c;
    sd.lista(Colhidos::ao_listar, &c, log);
    EXPECT_EQ(fs.montagens, fs.desmontagens);
}

TEST_F(CartaoSdNoHost, le_em_pedacos_ate_o_fim) {
    poe(0, "coruja.log", "abcdefghij");
    std::size_t tamanho = 0;
    ASSERT_EQ(sd.abre_para_leitura("coruja.log", &tamanho, log),
              ErroCartao::Nenhum);
    EXPECT_EQ(tamanho, 10u);

    std::string tudo;
    std::uint8_t buf[4];
    for (;;) {
        std::size_t n = 0;
        ASSERT_TRUE(sd.le(buf, sizeof buf, &n));
        if (n == 0) { break; }
        tudo.append(reinterpret_cast<char*>(buf), n);
    }
    sd.fecha_leitura();
    EXPECT_EQ(tudo, "abcdefghij");
}

TEST_F(CartaoSdNoHost, rebobina_devolve_o_mesmo_conteudo) {
    // A remessa lê cada arquivo duas vezes: CRC e envio. Se a segunda
    // passada viesse diferente, o CRC nunca bateria e nada seria apagado.
    poe(0, "20261006_143000.log", "conteudo da viagem");
    std::size_t tamanho = 0;
    ASSERT_EQ(sd.abre_para_leitura("20261006_143000.log", &tamanho, log),
              ErroCartao::Nenhum);

    auto passada = [&] {
        std::string s;
        std::uint8_t buf[8];
        for (;;) {
            std::size_t n = 0;
            if (!sd.le(buf, sizeof buf, &n) || n == 0) { break; }
            s.append(reinterpret_cast<char*>(buf), n);
        }
        return s;
    };
    const std::string primeira = passada();
    ASSERT_TRUE(sd.rebobina());
    const std::string segunda = passada();
    sd.fecha_leitura();

    EXPECT_EQ(primeira, "conteudo da viagem");
    EXPECT_EQ(segunda, primeira);
}

TEST_F(CartaoSdNoHost, abrir_arquivo_que_nao_existe_e_ausente_e_nao_falha) {
    // A remessa listou o cartão antes; o arquivo pode ter sumido no meio.
    // Distinguir importa: "sumiu" é normal, "não li" é defeito.
    fs.volumes[0].monta = true;
    std::size_t tamanho = 0;
    EXPECT_EQ(sd.abre_para_leitura("nao_existe.log", &tamanho, log),
              ErroCartao::ArquivoAusente);
}

TEST_F(CartaoSdNoHost, nao_abre_duas_leituras_ao_mesmo_tempo) {
    poe(0, "coruja.log", "a");
    poe(0, "infracoes.log", "b");
    std::size_t t1 = 0, t2 = 0;
    ASSERT_EQ(sd.abre_para_leitura("coruja.log", &t1, log), ErroCartao::Nenhum);
    EXPECT_EQ(sd.abre_para_leitura("infracoes.log", &t2, log),
              ErroCartao::FalhaDeLeitura);
    sd.fecha_leitura();
}

TEST_F(CartaoSdNoHost, ler_sem_abrir_nao_quebra) {
    std::uint8_t buf[4];
    std::size_t n = 0;
    EXPECT_FALSE(sd.le(buf, sizeof buf, &n));
    EXPECT_FALSE(sd.rebobina());
    sd.fecha_leitura();  // idempotente: não pode explodir
}

TEST_F(CartaoSdNoHost, a_leitura_desmonta_ao_fechar) {
    poe(0, "coruja.log", "dado");
    std::size_t tamanho = 0;
    sd.abre_para_leitura("coruja.log", &tamanho, log);
    const int montado = fs.montagens - fs.desmontagens;
    EXPECT_EQ(montado, 1);
    sd.fecha_leitura();
    EXPECT_EQ(fs.montagens, fs.desmontagens);
}

TEST_F(CartaoSdNoHost, o_log_pode_gravar_durante_uma_leitura_aberta) {
    // Por que o `CartaoSd` tem dois descritores. Com um só, a primeira linha
    // de log gravada no cartão destruiria o handle da leitura em curso -- e o
    // envio continuaria, mandando lixo.
    poe(0, "20261006_143000.log", "viagem inteira aqui");
    std::size_t tamanho = 0;
    ASSERT_EQ(sd.abre_para_leitura("20261006_143000.log", &tamanho, log),
              ErroCartao::Nenhum);

    ASSERT_EQ(sd.acrescenta_arquivo("coruja.log", "linha nova\n", 11, log),
              ErroCartao::Nenhum);

    std::string resto;
    std::uint8_t buf[32];
    for (;;) {
        std::size_t n = 0;
        if (!sd.le(buf, sizeof buf, &n) || n == 0) { break; }
        resto.append(reinterpret_cast<char*>(buf), n);
    }
    sd.fecha_leitura();
    EXPECT_EQ(resto, "viagem inteira aqui");
}

TEST_F(CartaoSdNoHost, remove_apaga_mesmo) {
    poe(0, "coruja.log", "dado");
    EXPECT_EQ(sd.remove("coruja.log", log), ErroCartao::Nenhum);
    EXPECT_FALSE(fs.existe(0, "coruja.log"));
}

TEST_F(CartaoSdNoHost, remover_o_que_nao_existe_e_ausente_e_nao_sucesso) {
    // Quem mandou apagar acreditava que o arquivo estava lá. A diferença
    // entre "apaguei" e "não havia nada" importa no log.
    fs.volumes[0].monta = true;
    EXPECT_EQ(sd.remove("nao_existe.log", log), ErroCartao::ArquivoAusente);
}

TEST_F(CartaoSdNoHost, falha_do_unlink_vira_falha_de_escrita) {
    poe(0, "coruja.log", "dado");
    fs.erro_unlink = FR_DENIED;
    EXPECT_EQ(sd.remove("coruja.log", log), ErroCartao::FalhaDeEscrita);
    EXPECT_TRUE(fs.existe(0, "coruja.log"));
}

TEST_F(CartaoSdNoHost, remove_desmonta_no_fim) {
    poe(0, "coruja.log", "dado");
    sd.remove("coruja.log", log);
    EXPECT_EQ(fs.montagens, fs.desmontagens);
}


// ======================= le_em_fluxo: por onde a base entra no boot

TEST_F(CartaoSdNoHost, falha_de_leitura_em_fluxo_e_acusada) {
    // Sem isto, um cartão que falha na metade devolveria `Nenhum` e o boot
    // carregaria uma base truncada em silêncio -- com a tela voltando ao
    // velocímetro normalmente e nada indicando radar faltando.
    poe(0, "radares.bin", std::string(4096, 'x'));
    fs.leitura_falha_apos = 1024;
    std::size_t lidos = 0;
    EXPECT_EQ(sd.le_em_fluxo("radares.bin", nullptr, nullptr, &lidos, log),
              ErroCartao::FalhaDeLeitura);
}

TEST_F(CartaoSdNoHost, leitura_em_fluxo_que_vai_bem_entrega_tudo) {
    const std::string conteudo(4096, 'y');
    poe(0, "radares.bin", conteudo);
    std::string recebido;
    auto ao_ler = [](void* ctx, const std::uint8_t* b, std::size_t n) {
        static_cast<std::string*>(ctx)->append(
            reinterpret_cast<const char*>(b), n);
    };
    std::size_t lidos = 0;
    ASSERT_EQ(sd.le_em_fluxo("radares.bin", ao_ler, &recebido, &lidos, log),
              ErroCartao::Nenhum);
    EXPECT_EQ(lidos, conteudo.size());
    EXPECT_EQ(recebido, conteudo);
}


// ============ a montagem aninhada: o incidente de 07/10/2026 ==============

/// Um logger que grava NO CARTAO, como o `LoggerCartao` faz ao descarregar.
///
/// É o arranjo exato do aparelho, e é o que faltava nos testes: o `LoggerMock`
/// não toca no cartão, então nenhum teste exercitava uma gravação de log
/// acontecendo DENTRO de outra operação do cartão.
struct LoggerQueGravaNoCartao final : public Logger {
    CartaoSd* sd = nullptr;
    int       gravacoes = 0;

    void define_nivel_minimo(Nivel) override {}

    void registra(Nivel, const char*, const char*) override {
        if (sd == nullptr) { return; }
        ++gravacoes;
        CartaoSd* alvo = sd;
        sd = nullptr;        // sem recursão: a gravação também registra
        teste::LoggerMock vala;
        alvo->acrescenta_arquivo("coruja.log", "linha\n", 6, vala);
        sd = alvo;
    }
};

TEST_F(CartaoSdNoHost, log_no_meio_do_promove_nao_derruba_a_troca_atomica) {
    // O que aconteceu no aparelho em 07/10/2026, reproduzido:
    //
    //   f_rename(radares.bin -> radares.bak)   OK
    //   log.info("base anterior guardada...")  <- descarregou no cartao
    //   f_rename(radares.tmp -> radares.bin)   FR_NOT_ENABLED
    //
    // O cartao ficou com .bak e .tmp, e SEM radares.bin. O aparelho perdeu a
    // base, e o log so disse "The volume has no work area (mount)".
    poe(0, "radares.bin", "base antiga");
    fs.volumes[0].arquivos["radares.tmp"] = "base nova";

    LoggerQueGravaNoCartao espiao;
    espiao.sd = &sd;

    ASSERT_EQ(sd.promove("radares.tmp", "radares.bin", "radares.bak", espiao),
              ErroCartao::Nenhum);
    EXPECT_GT(espiao.gravacoes, 0) << "o teste nao exercitou o que pretendia";
    EXPECT_EQ(fs.conteudo(0, "radares.bin"), "base nova");
    EXPECT_EQ(fs.conteudo(0, "radares.bak"), "base antiga");
    EXPECT_FALSE(fs.existe(0, "radares.tmp"));
}

TEST_F(CartaoSdNoHost, log_no_meio_do_envio_nao_mata_o_fluxo_de_leitura) {
    // A outra metade do mesmo incidente. O envio abre o arquivo da viagem em
    // fluxo e o le duas vezes (CRC e depois o corpo). Uma descarga de log no
    // meio desmontava o volume e o `f_read` seguinte morria -- o envio
    // "levava algum tempo e depois dava erro", sem uma linha no cartao.
    poe(0, "20261006_143000.log", "viagem inteira aqui");
    std::size_t tamanho = 0;
    ASSERT_EQ(sd.abre_para_leitura("20261006_143000.log", &tamanho, log),
              ErroCartao::Nenhum);

    teste::LoggerMock vala;
    ASSERT_EQ(sd.acrescenta_arquivo("coruja.log", "linha\n", 6, vala),
              ErroCartao::Nenhum);
    ASSERT_TRUE(sd.rebobina()) << "o rebobinar ja encontra o volume morto";

    std::string tudo;
    std::uint8_t buf[8];
    for (;;) {
        std::size_t n = 0;
        if (!sd.le(buf, sizeof buf, &n) || n == 0) { break; }
        tudo.append(reinterpret_cast<char*>(buf), n);
    }
    sd.fecha_leitura();
    EXPECT_EQ(tudo, "viagem inteira aqui");
}

TEST_F(CartaoSdNoHost, a_montagem_aninhada_desmonta_UMA_vez_so) {
    // A contagem tem de fechar: desmontar cedo demais derruba quem está por
    // fora, e desmontar de menos deixa o cartão montado quando ele pode ser
    // removido do aparelho.
    poe(0, "20261006_143000.log", "dados");
    std::size_t tamanho = 0;
    ASSERT_EQ(sd.abre_para_leitura("20261006_143000.log", &tamanho, log),
              ErroCartao::Nenhum);
    const int desmontagens_antes = fs.desmontagens;

    teste::LoggerMock vala;
    ASSERT_EQ(sd.acrescenta_arquivo("coruja.log", "linha\n", 6, vala),
              ErroCartao::Nenhum);
    EXPECT_EQ(fs.desmontagens, desmontagens_antes)
        << "a operacao de dentro desmontou o volume da de fora";

    sd.fecha_leitura();
    EXPECT_EQ(fs.desmontagens, desmontagens_antes + 1);
    EXPECT_EQ(fs.montagens, fs.desmontagens);
    EXPECT_FALSE(fs.volumes[0].montado) << "o cartao ficou montado no fim";
}

}  // namespace
