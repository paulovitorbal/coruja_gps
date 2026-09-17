#include "nucleo/BaseRadares.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "apoio/LoggerMock.h"
#include "nucleo/Geo.h"

namespace {

using namespace coruja;

/// Monta um arquivo valido em memoria, para que cada teste corrompa um campo
/// de cada vez e confirme que a validacao pega exatamente aquele.
class Construtor {
public:
    void adiciona(std::int32_t lat_e, std::int32_t lon_e, std::uint8_t limite,
                  std::uint8_t rumo_q, std::uint8_t tipo, std::uint8_t sentido) {
        const std::uint8_t flags =
            static_cast<std::uint8_t>((sentido & 0x03U) | ((tipo & 0x07U) << 2));
        escreve_i32(registros_, lat_e);
        escreve_i32(registros_, lon_e);
        registros_.push_back(limite);
        registros_.push_back(rumo_q);
        registros_.push_back(flags);
        registros_.push_back(0);
    }

    std::vector<std::uint8_t> constroi() const {
        std::vector<std::uint8_t> out;
        escreve_u32(out, kMagic);
        escreve_u16(out, kVersao);
        out.push_back(kExpoenteEscala);
        out.push_back(kTamRegistro);
        escreve_u32(out, static_cast<std::uint32_t>(registros_.size() / kTamRegistro));
        escreve_u32(out, crc32(registros_.data(), registros_.size()));
        out.insert(out.end(), registros_.begin(), registros_.end());
        return out;
    }

private:
    static void escreve_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
        v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
        v.push_back((x >> 16) & 0xFF); v.push_back((x >> 24) & 0xFF);
    }
    static void escreve_u16(std::vector<std::uint8_t>& v, std::uint16_t x) {
        v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
    }
    static void escreve_i32(std::vector<std::uint8_t>& v, std::int32_t x) {
        escreve_u32(v, static_cast<std::uint32_t>(x));
    }
    std::vector<std::uint8_t> registros_;
};

Construtor tres_pontos_validos() {
    Construtor c;
    c.adiciona(-1600000, -4800000, 60, 45, 1, 1);
    c.adiciona(-1578010, -4792920, 0, 0, 3, 0);
    c.adiciona(-1500000, -4790000, 110, 90, 5, 2);
    return c;
}

// --- caminho felizinho ---

TEST(CarregaBase, ArquivoValidoCarregaTudo) {
    const auto bytes = tres_pontos_validos().constroi();
    Ponto destino[8]{};
    teste::LoggerMock log;

    const auto r = carrega_base(bytes.data(), bytes.size(), destino, 8, &log);

    ASSERT_TRUE(r.ok()) << descreve(r.erro);
    EXPECT_EQ(r.pontos, 3U);
    EXPECT_TRUE(log.contem(Nivel::Info, "3 pontos"));
}

TEST(CarregaBase, DecodificaOsCamposCorretamente) {
    const auto bytes = tres_pontos_validos().constroi();
    Ponto d[8]{};
    ASSERT_TRUE(carrega_base(bytes.data(), bytes.size(), d, 8).ok());

    EXPECT_NEAR(d[0].lat, -16.0F, 1e-5F);
    EXPECT_NEAR(d[0].lon, -48.0F, 1e-5F);
    EXPECT_EQ(d[0].limite, 60);
    EXPECT_EQ(d[0].rumo, 90);  // quantizado: 45 * 2
    EXPECT_EQ(d[0].tipo, TipoPonto::RadarFixo);
    EXPECT_EQ(d[0].sentido, Sentido::Unidirecional);

    EXPECT_EQ(d[1].tipo, TipoPonto::SemaforoCamera);
    EXPECT_EQ(d[1].limite, kSemLimite);
    EXPECT_FALSE(afere_velocidade(d[1].tipo));
    EXPECT_TRUE(e_semaforo(d[1].tipo));

    EXPECT_EQ(d[2].tipo, TipoPonto::RadarMovel);
    EXPECT_EQ(d[2].sentido, Sentido::Bidirecional);
}

TEST(CarregaBase, RejeitaBaseVazia) {
    // A especificacao recusa n_pontos == 0 (formato_dados.md §2). Base vazia
    // nao e "nenhum radar por perto", e conversor com bug.
    Construtor c;
    const auto bytes = c.constroi();
    Ponto d[1]{};
    EXPECT_EQ(carrega_base(bytes.data(), bytes.size(), d, 1).erro,
              ErroBase::BaseVazia);
}

TEST(CarregaBase, RejeitaTamRegistroDiferente) {
    auto bytes = tres_pontos_validos().constroi();
    bytes[7] = 8;
    Ponto d[8]{};
    EXPECT_EQ(carrega_base(bytes.data(), bytes.size(), d, 8).erro,
              ErroBase::TamRegistroInvalido);
}

TEST(CarregaBase, RejeitaAcimaDoTetoDeMemoria) {
    // Declara 40.001 pontos: acima do teto a carga integral estouraria a RAM,
    // e o firmware recusa em vez de travar.
    auto bytes = tres_pontos_validos().constroi();
    const std::uint32_t acima = kTetoPontos + 1;
    bytes[8]  = acima & 0xFF;        bytes[9]  = (acima >> 8) & 0xFF;
    bytes[10] = (acima >> 16) & 0xFF; bytes[11] = (acima >> 24) & 0xFF;
    Ponto d[8]{};
    EXPECT_EQ(carrega_base(bytes.data(), bytes.size(), d, 8).erro,
              ErroBase::ExcedeuTeto);
}

// --- cada validacao, isolada ---

TEST(CarregaBase, RejeitaPonteiroNulo) {
    Ponto d[1]{};
    EXPECT_EQ(carrega_base(nullptr, 100, d, 1).erro, ErroBase::TamanhoInvalido);
    const auto b = tres_pontos_validos().constroi();
    EXPECT_EQ(carrega_base(b.data(), b.size(), nullptr, 1).erro,
              ErroBase::TamanhoInvalido);
}

TEST(CarregaBase, RejeitaArquivoMenorQueOCabecalho) {
    const std::uint8_t curto[8] = {};
    Ponto d[1]{};
    EXPECT_EQ(carrega_base(curto, sizeof curto, d, 1).erro,
              ErroBase::TamanhoInvalido);
}

TEST(CarregaBase, RejeitaTamanhoNaoMultiploDoRegistro) {
    auto bytes = tres_pontos_validos().constroi();
    bytes.pop_back();
    Ponto d[8]{};
    EXPECT_EQ(carrega_base(bytes.data(), bytes.size(), d, 8).erro,
              ErroBase::TamanhoInvalido);
}

TEST(CarregaBase, RejeitaMagicErrado) {
    auto bytes = tres_pontos_validos().constroi();
    bytes[0] = 'X';
    Ponto d[8]{};
    EXPECT_EQ(carrega_base(bytes.data(), bytes.size(), d, 8).erro,
              ErroBase::MagicInvalido);
}

TEST(CarregaBase, RejeitaVersaoFutura) {
    auto bytes = tres_pontos_validos().constroi();
    bytes[4] = 2;  // versao e u16 no offset 4
    Ponto d[8]{};
    EXPECT_EQ(carrega_base(bytes.data(), bytes.size(), d, 8).erro,
              ErroBase::VersaoInvalida);
}

TEST(CarregaBase, RejeitaEscalaDiferente) {
    auto bytes = tres_pontos_validos().constroi();
    bytes[6] = 6;
    Ponto d[8]{};
    EXPECT_EQ(carrega_base(bytes.data(), bytes.size(), d, 8).erro,
              ErroBase::EscalaInvalida);
}

TEST(CarregaBase, RejeitaContagemQueDiscordaDoTamanho) {
    auto bytes = tres_pontos_validos().constroi();
    bytes[8] = 9;  // declara 9, ha 3 (dentro do teto, para isolar este erro)
    Ponto d[16]{};
    EXPECT_EQ(carrega_base(bytes.data(), bytes.size(), d, 16).erro,
              ErroBase::ContagemInconsistente);
}

TEST(CarregaBase, RejeitaBaseMaiorQueACapacidade) {
    const auto bytes = tres_pontos_validos().constroi();
    Ponto d[2]{};
    EXPECT_EQ(carrega_base(bytes.data(), bytes.size(), d, 2).erro,
              ErroBase::ExcedeuCapacidade);
}

TEST(CarregaBase, RejeitaCrcErrado) {
    auto bytes = tres_pontos_validos().constroi();
    bytes[kTamCabecalho + 2] ^= 0xFF;  // corrompe um byte de dado
    Ponto d[8]{};
    teste::LoggerMock log;
    EXPECT_EQ(carrega_base(bytes.data(), bytes.size(), d, 8, &log).erro,
              ErroBase::CrcInvalido);
    EXPECT_EQ(log.contagem(Nivel::Error), 1U);
}

TEST(CarregaBase, RejeitaForaDeOrdemPorLatitude) {
    Construtor c;
    c.adiciona(-1500000, -4790000, 60, 0, 1, 1);
    c.adiciona(-1600000, -4800000, 60, 0, 1, 1);  // latitude menor depois
    const auto bytes = c.constroi();
    Ponto d[8]{};
    EXPECT_EQ(carrega_base(bytes.data(), bytes.size(), d, 8).erro,
              ErroBase::ForaDeOrdem);
}

TEST(CarregaBase, AceitaLatitudesIguaisEmSequencia) {
    Construtor c;
    c.adiciona(-1578010, -4792920, 60, 0, 1, 1);
    c.adiciona(-1578010, -4792900, 60, 0, 1, 1);
    const auto bytes = c.constroi();
    Ponto d[8]{};
    EXPECT_TRUE(carrega_base(bytes.data(), bytes.size(), d, 8).ok());
}

TEST(CarregaBase, RejeitaTipoForaDoDominio) {
    for (std::uint8_t tipo : {0, 4, 6, 7}) {
        Construtor c;
        c.adiciona(-1600000, -4800000, 60, 0, tipo, 1);
        const auto bytes = c.constroi();
        Ponto d[8]{};
        EXPECT_EQ(carrega_base(bytes.data(), bytes.size(), d, 8).erro,
                  ErroBase::RegistroInvalido)
            << "tipo " << int(tipo) << " deveria ser rejeitado";
    }
}

TEST(CarregaBase, RejeitaSentidoForaDoDominio) {
    Construtor c;
    c.adiciona(-1600000, -4800000, 60, 0, 1, 3);
    const auto bytes = c.constroi();
    Ponto d[8]{};
    EXPECT_EQ(carrega_base(bytes.data(), bytes.size(), d, 8).erro,
              ErroBase::RegistroInvalido);
}

TEST(CarregaBase, NaoCarregaParcialEmCasoDeErro) {
    // Base parcial e pior que base nenhuma: um alerta que falta e silencioso.
    auto bytes = tres_pontos_validos().constroi();
    bytes[kTamCabecalho + 1] ^= 0xFF;
    Ponto d[8]{};
    const auto r = carrega_base(bytes.data(), bytes.size(), d, 8);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.pontos, 0U);
}

TEST(Descreve, TodosOsErrosTemMensagem) {
    for (auto e : {ErroBase::TamanhoInvalido, ErroBase::MagicInvalido,
                   ErroBase::VersaoInvalida, ErroBase::EscalaInvalida,
                   ErroBase::TamRegistroInvalido, ErroBase::BaseVazia,
                   ErroBase::ExcedeuTeto, ErroBase::ContagemInconsistente,
                   ErroBase::ExcedeuCapacidade, ErroBase::CrcInvalido,
                   ErroBase::ForaDeOrdem, ErroBase::RegistroInvalido}) {
        EXPECT_STRNE(descreve(e), "erro desconhecido");
    }
}

// --- CRC-32 contra vetores conhecidos de zlib.crc32 ---

TEST(Crc32, ConfereComVetoresDeZlib) {
    EXPECT_EQ(crc32(nullptr, 0), 0U);
    const auto* a = reinterpret_cast<const std::uint8_t*>("a");
    EXPECT_EQ(crc32(a, 1), 0xE8B7BE43U);
    const auto* s = reinterpret_cast<const std::uint8_t*>("123456789");
    EXPECT_EQ(crc32(s, 9), 0xCBF43926U);
}

// --- contra a fixture sintetica de 100 pontos: obrigatoria ---

/// Carrega um radares.bin de disco. Compartilhada pelas duas fixtures.
std::vector<std::uint8_t> le_arquivo(const char* caminho) {
    std::FILE* f = std::fopen(caminho, "rb");
    if (f == nullptr) {
        return {};
    }
    std::fseek(f, 0, SEEK_END);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(std::ftell(f)));
    std::rewind(f);
    const auto lidos = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (lidos != bytes.size()) {
        return {};
    }
    return bytes;
}

class Fixture : public ::testing::Test {
protected:
    void SetUp() override {
        bytes_ = le_arquivo(CORUJA_FIXTURE_BIN);
        ASSERT_FALSE(bytes_.empty())
            << "fixture ausente em " << CORUJA_FIXTURE_BIN
            << "; o CMake deveria ter gerado com converte.py";
        pontos_.resize(kTetoPontos);
        const auto r = carrega_base(bytes_.data(), bytes_.size(), pontos_.data(),
                                    pontos_.size());
        ASSERT_TRUE(r.ok()) << descreve(r.erro);
        n_ = r.pontos;
    }
    std::vector<std::uint8_t> bytes_;
    std::vector<Ponto>        pontos_;
    std::size_t               n_ = 0;
};

TEST_F(Fixture, CarregaOsCemPontos) {
    EXPECT_EQ(n_, 100U);
}

TEST_F(Fixture, DistribuicaoDeTiposEADaFixture) {
    int fixo = 0, sem_radar = 0, camera = 0, movel = 0;
    for (std::size_t i = 0; i < n_; ++i) {
        switch (pontos_[i].tipo) {
            case TipoPonto::RadarFixo:        ++fixo; break;
            case TipoPonto::SemaforoComRadar: ++sem_radar; break;
            case TipoPonto::SemaforoCamera:   ++camera; break;
            case TipoPonto::RadarMovel:       ++movel; break;
        }
    }
    EXPECT_EQ(fixo, 43);
    EXPECT_EQ(sem_radar, 20);
    EXPECT_EQ(camera, 17);
    EXPECT_EQ(movel, 20);
}

TEST_F(Fixture, CobreOsTresSentidos) {
    int omni = 0, uni = 0, bi = 0;
    for (std::size_t i = 0; i < n_; ++i) {
        switch (pontos_[i].sentido) {
            case Sentido::Omnidirecional: ++omni; break;
            case Sentido::Unidirecional:  ++uni; break;
            case Sentido::Bidirecional:   ++bi; break;
        }
    }
    EXPECT_EQ(omni, 19);
    EXPECT_EQ(uni, 62);
    EXPECT_EQ(bi, 19);
}

TEST_F(Fixture, CobreTodosOsLimitesDaBaseIncluindoAcimaDeCem) {
    std::set<int> limites;
    int acima_de_cem = 0;
    for (std::size_t i = 0; i < n_; ++i) {
        limites.insert(pontos_[i].limite);
        if (pontos_[i].limite > 100) {
            ++acima_de_cem;
        }
    }
    const std::set<int> esperados = {0, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};
    EXPECT_EQ(limites, esperados);
    // Sem isto o ramo percentual do V_infra nunca seria exercitado.
    EXPECT_EQ(acima_de_cem, 7);
}

TEST_F(Fixture, SemLimiteSoAcontecemEmCameraDeSemaforo) {
    int sem_limite = 0, camera_com_limite = 0;
    for (std::size_t i = 0; i < n_; ++i) {
        const auto& p = pontos_[i];
        if (p.limite == kSemLimite) {
            ++sem_limite;
            EXPECT_EQ(p.tipo, TipoPonto::SemaforoCamera) << "ponto " << i;
            EXPECT_FALSE(afere_velocidade(p.tipo));
        } else if (p.tipo == TipoPonto::SemaforoCamera) {
            ++camera_com_limite;
        }
    }
    EXPECT_EQ(sem_limite, 14);
    // A inconsistencia dos 22 pontos da origem, preservada na fixture.
    EXPECT_EQ(camera_com_limite, 3);
}

TEST_F(Fixture, TemRumosCruzandoOZeroParaOTesteDeWraparound) {
    int perto_do_zero = 0;
    for (std::size_t i = 0; i < n_; ++i) {
        const auto r = pontos_[i].rumo;
        if (pontos_[i].sentido != Sentido::Omnidirecional && (r <= 4 || r >= 356)) {
            ++perto_do_zero;
        }
    }
    EXPECT_GE(perto_do_zero, 4) << "a fixture perdeu os rumos de wraparound";
}

TEST_F(Fixture, TemLatitudesIdenticasParaODesempateDaBuscaBinaria) {
    int pares_iguais = 0;
    for (std::size_t i = 1; i < n_; ++i) {
        if (pontos_[i].lat == pontos_[i - 1].lat) {
            ++pares_iguais;
        }
    }
    EXPECT_GE(pares_iguais, 3);
}

TEST_F(Fixture, EstaOrdenadaEDentroDoOceanoSintetico) {
    for (std::size_t i = 0; i < n_; ++i) {
        EXPECT_GE(pontos_[i].lat, -32.001F);
        EXPECT_LE(pontos_[i].lat, -21.999F);
        EXPECT_GE(pontos_[i].lon, -40.001F);
        EXPECT_LE(pontos_[i].lon, -34.999F);
        EXPECT_LT(pontos_[i].rumo, 360);
        if (i > 0) {
            EXPECT_GE(pontos_[i].lat, pontos_[i - 1].lat);
        }
    }
}

TEST_F(Fixture, OsRumosSobrevivemAQuantizacaoEmPassosDeDois) {
    // O formato guarda rumo/2 em um byte, entao todo rumo lido e par.
    for (std::size_t i = 0; i < n_; ++i) {
        EXPECT_EQ(pontos_[i].rumo % 2, 0) << "ponto " << i;
    }
}

// --- contra uma base completa, se o usuario tiver uma ---

class BaseCompleta : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::string(CORUJA_BASE_REAL).empty()) {
            GTEST_SKIP() << "nenhuma base completa configurada; passe "
                            "-DCORUJA_BASE_REAL=/caminho/radares.bin para "
                            "exercitar a carga em escala";
        }
        bytes_ = le_arquivo(CORUJA_BASE_REAL);
        if (bytes_.empty()) {
            GTEST_SKIP() << "base completa ilegivel em " << CORUJA_BASE_REAL;
        }
    }
    std::vector<std::uint8_t> bytes_;
};

TEST_F(BaseCompleta, CarregaEmEscalaSemErro) {
    std::vector<Ponto> pontos(kTetoPontos);
    teste::LoggerMock log;
    const auto r = carrega_base(bytes_.data(), bytes_.size(), pontos.data(),
                                pontos.size(), &log);
    ASSERT_TRUE(r.ok()) << descreve(r.erro);
    EXPECT_GT(r.pontos, 1000U);
    EXPECT_EQ(log.contagem(Nivel::Error), 0U);

    for (std::size_t i = 1; i < r.pontos; ++i) {
        ASSERT_GE(pontos[i].lat, pontos[i - 1].lat) << "desordem em " << i;
    }
}

}  // namespace
