#include "nucleo/VerificadorDownload.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using namespace coruja;

std::vector<std::uint8_t> le_fixture() {
    std::FILE* f = std::fopen(CORUJA_FIXTURE_BIN, "rb");
    EXPECT_NE(f, nullptr) << "fixture ausente: " << CORUJA_FIXTURE_BIN;
    std::vector<std::uint8_t> bytes;
    if (f == nullptr) {
        return bytes;
    }
    std::uint8_t bloco[4096];
    std::size_t  lidos = 0;
    while ((lidos = std::fread(bloco, 1, sizeof bloco, f)) > 0) {
        bytes.insert(bytes.end(), bloco, bloco + lidos);
    }
    std::fclose(f);
    return bytes;
}

/// Alimenta em pedaços de `passo`, simulando a fragmentação da rede.
ErroBase verifica_em_pedacos(const std::vector<std::uint8_t>& bytes,
                             std::size_t passo, VerificadorDownload* v) {
    for (std::size_t i = 0; i < bytes.size(); i += passo) {
        const std::size_t n = std::min(passo, bytes.size() - i);
        v->alimenta(bytes.data() + i, n);
    }
    return v->conclui(kCapacidadeFirmware);
}

TEST(VerificadorDownload, AceitaAFixtureReal) {
    const auto bytes = le_fixture();
    ASSERT_FALSE(bytes.empty());

    VerificadorDownload v;
    v.alimenta(bytes.data(), bytes.size());

    ASSERT_TRUE(v.tem_cabecalho());
    EXPECT_EQ(v.cabecalho().magic, kMagic);
    EXPECT_EQ(v.cabecalho().versao, kVersao);
    EXPECT_EQ(v.cabecalho().exp_escala, kExpoenteEscala);
    EXPECT_EQ(v.cabecalho().tam_registro, kTamRegistro);
    EXPECT_EQ(v.cabecalho().n_pontos, 100u);
    EXPECT_EQ(v.pontos_recebidos(), 100u);
    EXPECT_EQ(v.crc_calculado(), v.cabecalho().crc);
    EXPECT_EQ(v.conclui(kCapacidadeFirmware), ErroBase::Nenhum);
}

TEST(VerificadorDownload, OResultadoIndependeDoTamanhoDosPedacos) {
    // O ponto central da classe. 1 byte por vez parte o cabeçalho em 16
    // pedaços; 7 e 13 deixam a fronteira do cabeçalho no meio de um pedaço;
    // 1460 é o MSS típico de Ethernet, que é o caso real.
    const auto bytes = le_fixture();
    ASSERT_FALSE(bytes.empty());

    for (std::size_t passo : {1u, 7u, 13u, 16u, 17u, 512u, 1460u, 65536u}) {
        VerificadorDownload v;
        EXPECT_EQ(verifica_em_pedacos(bytes, passo, &v), ErroBase::Nenhum)
            << "passo " << passo;
        EXPECT_EQ(v.cabecalho().n_pontos, 100u) << "passo " << passo;
        EXPECT_EQ(v.crc_calculado(), v.cabecalho().crc) << "passo " << passo;
    }
}

TEST(VerificadorDownload, DownloadTruncadoEDetectado) {
    // A falha mais provável de todas: conexão caída no meio. O cabeçalho
    // promete 100 pontos e chegaram menos.
    auto bytes = le_fixture();
    ASSERT_FALSE(bytes.empty());
    bytes.resize(bytes.size() - kTamRegistro);

    VerificadorDownload v;
    v.alimenta(bytes.data(), bytes.size());
    EXPECT_EQ(v.pontos_recebidos(), 99u);
    EXPECT_EQ(v.conclui(kCapacidadeFirmware), ErroBase::ContagemInconsistente);
}

TEST(VerificadorDownload, ByteCorrompidoNoMeioDerrubaOCrc) {
    auto bytes = le_fixture();
    ASSERT_FALSE(bytes.empty());
    bytes[bytes.size() / 2] ^= 0x01U;

    VerificadorDownload v;
    v.alimenta(bytes.data(), bytes.size());
    EXPECT_EQ(v.conclui(kCapacidadeFirmware), ErroBase::CrcInvalido);
}

TEST(VerificadorDownload, RespostaDeErroDoServidorNaoPassaPorBase) {
    // Um proxy cativo devolvendo HTML com status 200 é o caso clássico.
    //
    // O erro exato depende do tamanho do HTML: com 44 bytes o tamanho já
    // reprova (44 - 16 não é múltiplo de 12) antes de o magic ser olhado. O
    // que importa é que não passe, e que o veredito seja o mesmo do
    // `carrega_base` — que reprova na mesma ordem.
    const char* html = "<html><head><title>404</title></head></html>";
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(html);

    VerificadorDownload v;
    v.alimenta(bytes, std::strlen(html));
    EXPECT_EQ(v.conclui(kCapacidadeFirmware), ErroBase::TamanhoInvalido);

    std::vector<Ponto> destino(8);
    EXPECT_EQ(carrega_base(bytes, std::strlen(html), destino.data(),
                           destino.size()).erro,
              v.conclui(kCapacidadeFirmware));
}

TEST(VerificadorDownload, ConteudoComTamanhoPlausivelEMagicErradoReprovaNoMagic) {
    // Mesmo comprimento de um arquivo válido, conteúdo que não é o nosso.
    std::vector<std::uint8_t> lixo(kTamCabecalho + 12 * 3, 0xAB);
    VerificadorDownload v;
    v.alimenta(lixo.data(), lixo.size());
    EXPECT_EQ(v.conclui(kCapacidadeFirmware), ErroBase::MagicInvalido);
}

TEST(VerificadorDownload, RespostaVaziaCaiEmTamanhoInvalido) {
    VerificadorDownload v;
    EXPECT_EQ(v.conclui(kCapacidadeFirmware), ErroBase::TamanhoInvalido);
    EXPECT_FALSE(v.tem_cabecalho());
    EXPECT_EQ(v.bytes_recebidos(), 0u);
}

TEST(VerificadorDownload, ArquivoGrandeDemaisParaEstaPlacaEDistinguido) {
    // ExcedeuCapacidade não é corrupção: o arquivo está certo e é grande
    // demais para esta placa. A distinção existe no carrega_base e tem de
    // existir aqui também.
    auto bytes = le_fixture();
    ASSERT_FALSE(bytes.empty());
    VerificadorDownload v;
    v.alimenta(bytes.data(), bytes.size());
    EXPECT_EQ(v.conclui(50), ErroBase::ExcedeuCapacidade);
}

TEST(VerificadorDownload, ReiniciaPermiteSegundaTentativa) {
    // O modo de bancada baixa de novo a cada clique; o verificador tem de
    // voltar ao zero, senão o CRC da segunda tentativa carrega a primeira.
    const auto bytes = le_fixture();
    ASSERT_FALSE(bytes.empty());

    VerificadorDownload v;
    v.alimenta(bytes.data(), bytes.size());
    ASSERT_EQ(v.conclui(kCapacidadeFirmware), ErroBase::Nenhum);

    v.reinicia();
    EXPECT_EQ(v.bytes_recebidos(), 0u);
    v.alimenta(bytes.data(), bytes.size());
    EXPECT_EQ(v.conclui(kCapacidadeFirmware), ErroBase::Nenhum);
}

TEST(VerificadorDownload, ConcordaComOCarregaBaseNoMesmoArquivo) {
    // Os dois caminhos têm de dar o mesmo veredito para o mesmo arquivo,
    // senão o log de um contradiz o do outro.
    auto bytes = le_fixture();
    ASSERT_FALSE(bytes.empty());
    std::vector<Ponto> destino(kCapacidadeFirmware);

    for (std::size_t pos : {0u, 4u, 6u, 7u}) {
        auto corrompido = bytes;
        corrompido[pos] ^= 0xFFU;

        VerificadorDownload v;
        v.alimenta(corrompido.data(), corrompido.size());
        const auto pelo_fluxo = v.conclui(kCapacidadeFirmware);
        const auto pela_carga = carrega_base(corrompido.data(), corrompido.size(),
                                             destino.data(), destino.size()).erro;
        EXPECT_EQ(pelo_fluxo, pela_carga) << "byte " << pos;
        EXPECT_NE(pelo_fluxo, ErroBase::Nenhum) << "byte " << pos;
    }
}

}  // namespace
