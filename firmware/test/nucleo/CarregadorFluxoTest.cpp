#include "nucleo/CarregadorFluxo.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

using namespace coruja;

std::vector<std::uint8_t> le_fixture() {
    std::FILE* f = std::fopen(CORUJA_FIXTURE_BIN, "rb");
    EXPECT_NE(f, nullptr);
    std::vector<std::uint8_t> bytes;
    if (f == nullptr) return bytes;
    std::uint8_t bloco[4096];
    std::size_t n = 0;
    while ((n = std::fread(bloco, 1, sizeof bloco, f)) > 0)
        bytes.insert(bytes.end(), bloco, bloco + n);
    std::fclose(f);
    return bytes;
}

/// Alimenta em pedaços de `passo`, como o cartão entrega.
ErroBase carrega_em_pedacos(const std::vector<std::uint8_t>& bytes,
                            std::size_t passo, std::vector<Ponto>* destino) {
    CarregadorFluxo c(destino->data(), destino->size());
    for (std::size_t i = 0; i < bytes.size(); i += passo) {
        c.alimenta(bytes.data() + i, std::min(passo, bytes.size() - i));
    }
    return c.conclui();
}

TEST(CarregadorFluxo, CarregaAFixtureEDaOsMesmosPontosQueOCarregaBase) {
    const auto bytes = le_fixture();
    ASSERT_FALSE(bytes.empty());

    std::vector<Ponto> pelo_buffer(kCapacidadeFirmware);
    const auto r = carrega_base(bytes.data(), bytes.size(), pelo_buffer.data(),
                                pelo_buffer.size());
    ASSERT_EQ(r.erro, ErroBase::Nenhum);

    std::vector<Ponto> pelo_fluxo(kCapacidadeFirmware);
    CarregadorFluxo c(pelo_fluxo.data(), pelo_fluxo.size());
    c.alimenta(bytes.data(), bytes.size());
    ASSERT_EQ(c.conclui(), ErroBase::Nenhum);
    ASSERT_EQ(c.pontos(), r.pontos);

    // Não basta o veredito bater: os PONTOS têm de ser idênticos, byte a byte.
    for (std::size_t i = 0; i < r.pontos; ++i) {
        EXPECT_EQ(pelo_fluxo[i].lat, pelo_buffer[i].lat) << "ponto " << i;
        EXPECT_EQ(pelo_fluxo[i].lon, pelo_buffer[i].lon) << "ponto " << i;
        EXPECT_EQ(pelo_fluxo[i].limite, pelo_buffer[i].limite) << "ponto " << i;
        EXPECT_EQ(pelo_fluxo[i].rumo_q, pelo_buffer[i].rumo_q) << "ponto " << i;
        EXPECT_EQ(pelo_fluxo[i].tipo, pelo_buffer[i].tipo) << "ponto " << i;
        EXPECT_EQ(pelo_fluxo[i].sentido, pelo_buffer[i].sentido) << "ponto " << i;
    }
}

TEST(CarregadorFluxo, OTamanhoDoPedacoNaoMudaNada) {
    // 12 não divide 4096 nem 512: o registro parte entre leituras, e é o caso
    // que a montagem por `parcial_` existe para cobrir.
    const auto bytes = le_fixture();
    ASSERT_FALSE(bytes.empty());
    for (std::size_t passo : {1u, 5u, 12u, 13u, 16u, 17u, 512u, 4096u}) {
        std::vector<Ponto> pontos(kCapacidadeFirmware);
        EXPECT_EQ(carrega_em_pedacos(bytes, passo, &pontos), ErroBase::Nenhum)
            << "passo " << passo;
    }
}

TEST(CarregadorFluxo, ConcordaComOCarregaBaseEmArquivoCorrompido) {
    // Os dois caminhos precisam recusar pelo MESMO motivo, senão o log de um
    // contradiz o do outro e o diagnóstico depende de qual rodou.
    const auto original = le_fixture();
    ASSERT_FALSE(original.empty());

    // magic, versão, escala, tam_registro, contagem, um byte de dado, e o CRC
    for (std::size_t pos : {0u, 4u, 6u, 7u, 8u, 100u, 12u}) {
        auto bytes = original;
        bytes[pos] ^= 0xFFU;

        std::vector<Ponto> a(kCapacidadeFirmware), b(kCapacidadeFirmware);
        const auto pela_carga = carrega_base(bytes.data(), bytes.size(),
                                             a.data(), a.size()).erro;
        CarregadorFluxo c(b.data(), b.size());
        c.alimenta(bytes.data(), bytes.size());
        EXPECT_EQ(c.conclui(), pela_carga) << "byte " << pos;
        EXPECT_NE(pela_carga, ErroBase::Nenhum) << "byte " << pos;
    }
}

TEST(CarregadorFluxo, ArquivoTruncadoEDetectado) {
    auto bytes = le_fixture();
    ASSERT_FALSE(bytes.empty());
    bytes.resize(bytes.size() - kTamRegistro);

    std::vector<Ponto> pontos(kCapacidadeFirmware);
    CarregadorFluxo c(pontos.data(), pontos.size());
    c.alimenta(bytes.data(), bytes.size());
    EXPECT_EQ(c.conclui(), ErroBase::ContagemInconsistente);
}

TEST(CarregadorFluxo, NaoEstouraODestinoQuandoACapacidadeEMenor) {
    // O teste que protege a memória: com destino menor que o arquivo, ele tem
    // de parar de escrever e mesmo assim consumir tudo, para o CRC fechar e o
    // erro ser "grande demais" e não "corrompido".
    const auto bytes = le_fixture();
    ASSERT_FALSE(bytes.empty());

    constexpr std::size_t kPequeno = 10;
    std::vector<Ponto> pontos(kPequeno + 8, Ponto{});
    CarregadorFluxo c(pontos.data(), kPequeno);
    c.alimenta(bytes.data(), bytes.size());

    EXPECT_EQ(c.conclui(), ErroBase::ExcedeuCapacidade);
    EXPECT_LE(c.pontos(), kPequeno);
    for (std::size_t i = kPequeno; i < pontos.size(); ++i) {
        EXPECT_EQ(pontos[i].limite, 0) << "escreveu alem da capacidade em " << i;
    }
}

TEST(CarregadorFluxo, ReiniciaPermiteUsarOMesmoObjetoNaSegundaTentativa) {
    // O boot tenta o radares.bin e, falhando, o radares.bak — com o mesmo
    // carregador. Sem reiniciar, o CRC do segundo carregaria o primeiro.
    const auto bytes = le_fixture();
    ASSERT_FALSE(bytes.empty());
    std::vector<Ponto> pontos(kCapacidadeFirmware);
    CarregadorFluxo c(pontos.data(), pontos.size());

    auto corrompido = bytes;
    corrompido[50] ^= 0x01U;
    c.alimenta(corrompido.data(), corrompido.size());
    ASSERT_EQ(c.conclui(), ErroBase::CrcInvalido);

    c.reinicia();
    c.alimenta(bytes.data(), bytes.size());
    EXPECT_EQ(c.conclui(), ErroBase::Nenhum);
    EXPECT_EQ(c.pontos(), 100u);
}

}  // namespace
