#include "nucleo/CarregadorFluxo.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "apoio/CaoDeGuardaFalso.h"
#include "apoio/LoggerMock.h"

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

TEST(CarregadorFluxo, SemDestinoValidaSemEscreverNada) {
    // O primeiro estagio da troca da base: o mesmo veredito, sem um byte no
    // vetor que o alerta esta usando.
    const auto bytes = le_fixture();
    ASSERT_FALSE(bytes.empty());
    CarregadorFluxo c(nullptr, kCapacidadeFirmware);
    c.alimenta(bytes.data(), bytes.size());
    EXPECT_EQ(c.conclui(), ErroBase::Nenhum);
    EXPECT_EQ(c.pontos(), 100u);

    auto corrompido = bytes;
    corrompido[100] ^= 0x01U;
    c.reinicia();
    c.alimenta(corrompido.data(), corrompido.size());
    EXPECT_EQ(c.conclui(), ErroBase::CrcInvalido);
}

// ===========================================================================
// A troca da base em uso: so inteira, e so depois de validada
// ===========================================================================
//
// Depois de um OTA a base e relida do cartao para o MESMO vetor que o alerta
// usa -- nao ha RAM para dois. A versao anterior escrevia no vetor enquanto
// lia: se a leitura falhava no meio, a contagem antiga passava a apontar para
// um vetor misturado, metade base nova e metade velha, e o piloto alertava
// sobre ele sem que nada denunciasse.

/// Cartao de bases em memoria, com falha escolhida por passada.
struct CartaoDeBases {
    std::map<std::string, std::vector<std::uint8_t>> arquivos;
    unsigned leituras = 0;
    /// A leitura de numero N (1, 2, ...) para no meio e devolve falha.
    unsigned falha_na_leitura = 0;
    /// Quanto o relogio falso anda a cada pedaco entregue, e de que tamanho
    /// eles sao. Um cartao lento ou degradado e exatamente isto: pedaco a
    /// pedaco, o tempo passa.
    std::uint32_t* relogio = nullptr;
    std::uint32_t  custo_por_pedaco_ms = 0;
    std::size_t    tamanho_do_pedaco = 4096;

    static ErroCartao le(void* fonte, const char* nome, AoLerPedacoBase ao_ler,
                         void* contexto, Logger&) {
        auto* c = static_cast<CartaoDeBases*>(fonte);
        ++c->leituras;
        const auto it = c->arquivos.find(nome);
        if (it == c->arquivos.end()) { return ErroCartao::ArquivoAusente; }
        const auto& b = it->second;
        const bool falha = c->leituras == c->falha_na_leitura;
        const std::size_t ate = falha ? b.size() / 2 : b.size();
        const std::size_t passo = c->tamanho_do_pedaco;
        for (std::size_t i = 0; i < ate; i += passo) {
            if (c->relogio != nullptr) { *c->relogio += c->custo_por_pedaco_ms; }
            ao_ler(contexto, b.data() + i, std::min<std::size_t>(passo, ate - i));
        }
        return falha ? ErroCartao::FalhaDeLeitura : ErroCartao::Nenhum;
    }
};

struct TrocaDaBase : public ::testing::Test {
    CartaoDeBases      cartao;
    teste::LoggerMock  log;
    std::vector<Ponto> vetor = std::vector<Ponto>(kCapacidadeFirmware, Ponto{});
    std::vector<std::uint8_t> fixture = le_fixture();

    /// Uma base "em uso" que nao e a fixture: tres pontos reconheciveis.
    BaseCarregada poe_base_em_uso() {
        for (std::size_t i = 0; i < 3; ++i) {
            vetor[i] = Ponto{-1.0F * static_cast<float>(i + 1), -2.0F, 77, 0,
                             TipoPonto::RadarFixo, Sentido::Omnidirecional};
        }
        BaseCarregada b;
        b.pontos = 3;
        b.cabecalho.ano = 1999;
        return b;
    }

    BaseCarregada troca(const BaseCarregada& em_uso,
                        CaoDeGuarda* cao = nullptr) {
        return troca_base(&CartaoDeBases::le, &cartao, "radares.bin",
                          "radares.bak", vetor.data(), vetor.size(), em_uso,
                          log, cao);
    }

    bool vetor_igual(const std::vector<Ponto>& outro) const {
        return std::memcmp(vetor.data(), outro.data(),
                           vetor.size() * sizeof(Ponto)) == 0;
    }
};

TEST_F(TrocaDaBase, base_nova_que_nao_valida_nao_toca_na_base_em_uso) {
    ASSERT_FALSE(fixture.empty());
    const BaseCarregada em_uso = poe_base_em_uso();
    const auto antes = vetor;

    auto corrompida = fixture;
    corrompida[corrompida.size() - 5] ^= 0x01U;   // CRC so falha no fim
    cartao.arquivos["radares.bin"] = corrompida;

    const BaseCarregada depois = troca(em_uso);

    EXPECT_EQ(depois.pontos, em_uso.pontos);
    EXPECT_EQ(depois.cabecalho.ano, 1999);
    EXPECT_TRUE(vetor_igual(antes)) << "a base em uso foi misturada com a nova";
}

TEST_F(TrocaDaBase, base_preservada_deixa_de_dizer_que_e_a_vigente) {
    // `da_vigente` diz ao OTA se o `versao.txt` descreve a base EM USO. Depois
    // de um OTA o `promove` ja moveu o arquivo velho para o `.bak`: se
    // nenhum arquivo novo valida e a base em uso e mantida, ela segue na
    // memoria, mas o arquivo vigente ja NAO e o dela. Devolver a marca velha
    // (`true`) fazia o clique seguinte dizer "JA ESTA EM DIA" com o `.bin`
    // ruim no cartao e a base antiga rodando.
    ASSERT_FALSE(fixture.empty());
    BaseCarregada em_uso = poe_base_em_uso();
    em_uso.da_vigente = true;

    auto corrompida = fixture;
    corrompida[corrompida.size() - 5] ^= 0x01U;
    cartao.arquivos["radares.bin"] = corrompida;
    cartao.arquivos["radares.bak"] = corrompida;

    const BaseCarregada depois = troca(em_uso);

    EXPECT_EQ(depois.pontos, em_uso.pontos) << "a base em uso foi perdida";
    EXPECT_FALSE(depois.da_vigente)
        << "a base em uso nao e mais a do arquivo vigente";
}

TEST_F(TrocaDaBase, leitura_que_falha_no_meio_nao_toca_na_base_em_uso) {
    ASSERT_FALSE(fixture.empty());
    const BaseCarregada em_uso = poe_base_em_uso();
    const auto antes = vetor;
    cartao.arquivos["radares.bin"] = fixture;
    cartao.falha_na_leitura = 1;

    const BaseCarregada depois = troca(em_uso);

    EXPECT_EQ(depois.pontos, em_uso.pontos);
    EXPECT_TRUE(vetor_igual(antes)) << "a base em uso foi misturada com a nova";
}

TEST_F(TrocaDaBase, base_nova_valida_substitui_a_em_uso_inteira) {
    ASSERT_FALSE(fixture.empty());
    const BaseCarregada em_uso = poe_base_em_uso();
    cartao.arquivos["radares.bin"] = fixture;

    const BaseCarregada depois = troca(em_uso);

    std::vector<Ponto> esperado(kCapacidadeFirmware);
    const auto r = carrega_base(fixture.data(), fixture.size(),
                                esperado.data(), esperado.size());
    ASSERT_EQ(r.erro, ErroBase::Nenhum);
    ASSERT_EQ(depois.pontos, r.pontos);
    for (std::size_t i = 0; i < r.pontos; ++i) {
        ASSERT_EQ(vetor[i].lat, esperado[i].lat) << "ponto " << i;
        ASSERT_EQ(vetor[i].limite, esperado[i].limite) << "ponto " << i;
    }
}

TEST_F(TrocaDaBase, copia_que_falha_depois_de_validar_nao_deixa_a_contagem_velha) {
    // O unico caso em que o vetor se perde: o arquivo validou, e a SEGUNDA
    // leitura -- a que escreve -- falhou. O vetor esta misturado; a contagem
    // antiga sobre ele seria alertar com coordenadas de duas bases. Zero
    // pontos e "BASE INDISPONIVEL" na tela, que e o lado certo.
    ASSERT_FALSE(fixture.empty());
    const BaseCarregada em_uso = poe_base_em_uso();
    cartao.arquivos["radares.bin"] = fixture;
    cartao.falha_na_leitura = 2;

    const BaseCarregada depois = troca(em_uso);

    EXPECT_EQ(depois.pontos, 0u);
    EXPECT_TRUE(log.contem(Nivel::Error, "NENHUMA base"));
}

TEST_F(TrocaDaBase, copia_que_falha_cai_para_a_reserva) {
    ASSERT_FALSE(fixture.empty());
    const BaseCarregada em_uso = poe_base_em_uso();
    cartao.arquivos["radares.bin"] = fixture;
    cartao.arquivos["radares.bak"] = fixture;
    cartao.falha_na_leitura = 2;

    const BaseCarregada depois = troca(em_uso);

    EXPECT_EQ(depois.pontos, 100u);
    EXPECT_TRUE(log.contem(Nivel::Warning, "RESERVA"));
}

TEST_F(TrocaDaBase, sem_base_em_uso_le_o_arquivo_uma_vez_so) {
    // No boot nao ha o que proteger: o vetor esta vazio, e validar antes
    // custaria uma segunda leitura de 214 KB no arranque por nada.
    ASSERT_FALSE(fixture.empty());
    cartao.arquivos["radares.bin"] = fixture;

    const BaseCarregada depois = troca(BaseCarregada{});

    EXPECT_EQ(depois.pontos, 100u);
    EXPECT_EQ(cartao.leituras, 1u);
}

TEST_F(TrocaDaBase, sem_base_em_uso_e_sem_arquivo_fica_sem_base) {
    const BaseCarregada depois = troca(BaseCarregada{});
    EXPECT_EQ(depois.pontos, 0u);
    EXPECT_TRUE(log.contem(Nivel::Error, "NENHUMA base"));
}

// ---------------------------------------------------------------------------
// O cao de guarda na recarga pos-OTA. Ela le o `radares.bin` duas vezes (quatro
// se cair na reserva), com o laco principal parado, e o `le_em_fluxo` nao tem
// gancho por dentro: sem refeicao por pedaco, um cartao lento reiniciava o
// aparelho no meio da recarga.

class RelogioDoCartao : public Pausa {
public:
    std::uint32_t agora = 500'000;
    void espera_ms(std::uint32_t ms) override { agora += ms; }
    std::uint32_t agora_ms() override { return agora; }
};

TEST_F(TrocaDaBase, cada_pedaco_lido_alimenta_o_cao) {
    ASSERT_FALSE(fixture.empty());
    cartao.arquivos["radares.bin"] = fixture;
    cartao.tamanho_do_pedaco = 256;
    teste::CaoDeGuardaFalso cao;

    troca(BaseCarregada{}, &cao);

    const unsigned pedacos =
        static_cast<unsigned>((fixture.size() + 255) / 256);
    ASSERT_GT(pedacos, 3u);
    EXPECT_EQ(cao.alimentacoes, pedacos);
}

TEST_F(TrocaDaBase, cartao_lento_nas_quatro_passadas_nao_deixa_o_cao_sem_comer) {
    // Pior caso: base em uso (validacao + copia) e o `.bin` que cai na
    // reserva, que e lida duas vezes tambem -- quatro passadas.
    ASSERT_FALSE(fixture.empty());
    auto corrompida = fixture;
    corrompida[corrompida.size() - 5] ^= 0x01U;
    cartao.arquivos["radares.bin"] = corrompida;
    cartao.arquivos["radares.bak"] = fixture;
    RelogioDoCartao relogio;
    cartao.relogio = &relogio.agora;
    cartao.custo_por_pedaco_ms = 2'000;
    cartao.tamanho_do_pedaco = 64;
    teste::CaoCronometrado cao(relogio);

    const BaseCarregada depois = troca(poe_base_em_uso(), &cao);
    cao.fecha();

    EXPECT_EQ(depois.pontos, 100u);
    EXPECT_GT(relogio.agora - 500'000u, kPrazoCaoMs)
        << "o cenario nem passa do prazo: nao prova nada";
    EXPECT_LE(cao.maior_intervalo_ms, 2'000u)
        << "um trecho da recarga ficou " << cao.maior_intervalo_ms
        << " ms sem refeicao";
}

TEST_F(TrocaDaBase, sem_cao_a_troca_roda_igual) {
    ASSERT_FALSE(fixture.empty());
    cartao.arquivos["radares.bin"] = fixture;
    EXPECT_EQ(troca(BaseCarregada{}, nullptr).pontos, 100u);
}

}  // namespace
