#include "nucleo/Nmea.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

namespace {

using namespace coruja;

// Sentenças no formato do NEO-M8N, com o checksum **calculado**, não
// suposto: a primeira versão deste arquivo trazia checksums escritos à mão e
// os testes os recusaram — que é exatamente o serviço que o validador presta. As duas primeiras são o
// mesmo instante nos dois talkers — GN com multi-GNSS, GP em modo GPS-only.
constexpr const char* kRmcGn =
    "$GNRMC,123519.00,A,1947.99496,S,04401.26264,W,22.4,84.4,220926,,,A*46";
constexpr const char* kRmcGp =
    "$GPRMC,123519.00,A,1947.99496,S,04401.26264,W,22.4,84.4,220926,,,A*58";

ErroNmea analisa(const char* s, Telemetria* t) {
    return analisa_rmc(s, std::strlen(s), t);
}

/// Recalcula o checksum de uma sentença montada à mão, para os testes não
/// precisarem de aritmética hexadecimal no meio do caso.
std::string com_checksum(const std::string& corpo) {
    std::uint8_t cs = 0;
    for (std::size_t i = 1; i < corpo.size(); ++i) cs ^= corpo[i];
    char fim[8];
    std::snprintf(fim, sizeof fim, "*%02X", cs);
    return corpo + fim;
}

// ------------------------------------------------------------- checksum ----

TEST(Nmea, ChecksumDasSentencasDeReferencia) {
    EXPECT_TRUE(checksum_valido(kRmcGn, std::strlen(kRmcGn)));
    EXPECT_TRUE(checksum_valido(kRmcGp, std::strlen(kRmcGp)));
}

TEST(Nmea, ChecksumRecusaUmBitTrocado) {
    // Qualquer byte alterado no corpo tem de derrubar o checksum. É a única
    // defesa contra UART com ruído, e ela precisa valer em todo o corpo.
    const std::string base = kRmcGn;
    for (std::size_t i = 1; i + 3 < base.size(); ++i) {
        std::string ruim = base;
        ruim[i] = static_cast<char>(ruim[i] ^ 0x01);
        EXPECT_FALSE(checksum_valido(ruim.c_str(), ruim.size()))
            << "byte " << i << " alterado e o checksum passou";
    }
}

TEST(Nmea, ChecksumAceitaHexMinusculo) {
    std::string s = kRmcGn;
    s[s.size() - 2] = static_cast<char>(std::tolower(s[s.size() - 2]));
    s[s.size() - 1] = static_cast<char>(std::tolower(s[s.size() - 1]));
    EXPECT_TRUE(checksum_valido(s.c_str(), s.size()));
}

TEST(Nmea, ChecksumIgnoraTerminadorDeLinha) {
    // A UART entrega com CRLF, e ele não entra no cálculo.
    const std::string s = std::string(kRmcGn) + "\r\n";
    EXPECT_TRUE(checksum_valido(s.c_str(), s.size()));
    Telemetria t;
    EXPECT_EQ(analisa_rmc(s.c_str(), s.size(), &t), ErroNmea::Nenhum);
}

// -------------------------------------------------------------- talker -----

TEST(Nmea, OsDoisTalkersDaoOMesmoResultado) {
    // O RF01.1 existe por isto: o talker muda com o modo GNSS, e um parser
    // fixado em GN não casaria nada em modo GPS-only.
    Telemetria gn, gp;
    ASSERT_EQ(analisa(kRmcGn, &gn), ErroNmea::Nenhum);
    ASSERT_EQ(analisa(kRmcGp, &gp), ErroNmea::Nenhum);
    EXPECT_FLOAT_EQ(gn.lat, gp.lat);
    EXPECT_FLOAT_EQ(gn.lon, gp.lon);
    EXPECT_FLOAT_EQ(gn.velocidade_kmh, gp.velocidade_kmh);
}

TEST(Nmea, TalkerDesconhecidoAindaEAceito) {
    // Deliberado: recusar um talker novo custaria um fix, e o checksum é que
    // protege contra lixo. Ver a nota no cabeçalho.
    const std::string s = com_checksum(
        "$GLRMC,123519.00,A,1947.99496,S,04401.26264,W,22.4,84.4,220926,,,A");
    Telemetria t;
    EXPECT_EQ(analisa(s.c_str(), &t), ErroNmea::Nenhum);
}

TEST(Nmea, OutraSentencaERecusada) {
    const std::string gga = com_checksum(
        "$GNGGA,123519.00,1947.99496,S,04401.26264,W,1,08,0.9,545.4,M,46.9,M,,");
    Telemetria t;
    EXPECT_EQ(analisa(gga.c_str(), &t), ErroNmea::NaoEhRmc);
}

// ------------------------------------------------------------- conversao ---

TEST(Nmea, CoordenadasConvertidasDeGrausEMinutos) {
    // 1947.99496 = 19 graus + 47,99496 minutos = 19,7999160 graus.
    // Ao SUL, então negativo. O erro classico aqui e tratar o numero inteiro
    // como grau: daria 1947,99 em vez de 19,80.
    Telemetria t;
    ASSERT_EQ(analisa(kRmcGn, &t), ErroNmea::Nenhum);
    EXPECT_NEAR(t.lat, -19.799916F, 0.00002F);
    EXPECT_NEAR(t.lon, -44.021044F, 0.00002F);
}

TEST(Nmea, LatitudeELongitudeTemLarguraDeGrauDiferente) {
    // Latitude usa 2 digitos de grau, longitude usa 3. Trocar isso desloca a
    // posicao em dezenas de graus, e o resultado ainda parece plausivel.
    const std::string s = com_checksum(
        "$GNRMC,123519.00,A,0100.0000,N,00100.0000,E,0.0,,220926,,,A");
    Telemetria t;
    ASSERT_EQ(analisa(s.c_str(), &t), ErroNmea::Nenhum);
    EXPECT_NEAR(t.lat, 1.0F, 1e-5F);
    EXPECT_NEAR(t.lon, 1.0F, 1e-5F);
}

TEST(Nmea, HemisferiosNorteELeste) {
    const std::string s = com_checksum(
        "$GNRMC,123519.00,A,1947.99496,N,04401.26264,E,0.0,,220926,,,A");
    Telemetria t;
    ASSERT_EQ(analisa(s.c_str(), &t), ErroNmea::Nenhum);
    EXPECT_GT(t.lat, 0.0F);
    EXPECT_GT(t.lon, 0.0F);
}

TEST(Nmea, VelocidadeConvertidaDeNosParaKmh) {
    // 22,4 nós × 1,852 = 41,4848 km/h.
    Telemetria t;
    ASSERT_EQ(analisa(kRmcGn, &t), ErroNmea::Nenhum);
    EXPECT_NEAR(t.velocidade_kmh, 41.4848F, 0.001F);
}

TEST(Nmea, RumoLido) {
    Telemetria t;
    ASSERT_EQ(analisa(kRmcGn, &t), ErroNmea::Nenhum);
    EXPECT_TRUE(t.rumo_valido);
    EXPECT_NEAR(t.rumo_graus, 84.4F, 0.001F);
}

TEST(Nmea, DataEHoraUtc) {
    Telemetria t;
    ASSERT_EQ(analisa(kRmcGn, &t), ErroNmea::Nenhum);
    EXPECT_TRUE(t.data_valida);
    EXPECT_EQ(t.hora, 12); EXPECT_EQ(t.minuto, 35); EXPECT_EQ(t.segundo, 19);
    EXPECT_EQ(t.dia, 22);  EXPECT_EQ(t.mes, 9);     EXPECT_EQ(t.ano, 2026);
}

// ------------------------------------------------------- casos de campo ----

TEST(Nmea, RumoVazioComVeiculoParadoNaoEErro) {
    // O NEO-M8N deixa o rumo em branco sem deslocamento. Tratar isso como erro
    // descartaria todo fix com o carro parado; tratar como zero apontaria para
    // o norte.
    const std::string s = com_checksum(
        "$GNRMC,123519.00,A,1947.99496,S,04401.26264,W,0.0,,220926,,,A");
    Telemetria t;
    ASSERT_EQ(analisa(s.c_str(), &t), ErroNmea::Nenhum);
    EXPECT_FALSE(t.rumo_valido);
    EXPECT_FLOAT_EQ(t.velocidade_kmh, 0.0F);
}

TEST(Nmea, StatusVDevolveSemFixSemTocarNoDestino) {
    // Sem fix a sentença é íntegra e o dado é que falta. Não pode apagar a
    // última posição conhecida.
    const std::string s = com_checksum(
        "$GNRMC,123519.00,V,,,,,,,220926,,,N");
    Telemetria t;
    t.lat = -19.8F; t.lon = -44.0F;
    EXPECT_EQ(analisa(s.c_str(), &t), ErroNmea::SemFix);
    EXPECT_FLOAT_EQ(t.lat, -19.8F) << "o destino foi sobrescrito";
    EXPECT_FLOAT_EQ(t.lon, -44.0F);
}

TEST(Nmea, SemDataAPosicaoAindaVale) {
    // O relógio é conveniência; a posição é o que protege o motorista.
    const std::string s = com_checksum(
        "$GNRMC,,A,1947.99496,S,04401.26264,W,10.0,90.0,,,,A");
    Telemetria t;
    ASSERT_EQ(analisa(s.c_str(), &t), ErroNmea::Nenhum);
    EXPECT_FALSE(t.data_valida);
    EXPECT_NEAR(t.lat, -19.799916F, 0.00002F);
}

// ------------------------------------------------------------- recusas -----

TEST(Nmea, RecusasDeFormato) {
    Telemetria t;
    EXPECT_EQ(analisa_rmc(nullptr, 10, &t), ErroNmea::Vazia);
    EXPECT_EQ(analisa_rmc(kRmcGn, 0, &t), ErroNmea::Vazia);
    EXPECT_EQ(analisa_rmc(kRmcGn, std::strlen(kRmcGn), nullptr), ErroNmea::Vazia);
    EXPECT_EQ(analisa("GNRMC,1,A*00", &t), ErroNmea::SemCifrao);
    EXPECT_EQ(analisa("$GNRMC,123519.00,A,1947.9,S", &t), ErroNmea::SemAsterisco);
    EXPECT_EQ(analisa("$GNRMC,123519.00,A,1947.9,S*Z9", &t),
              ErroNmea::ChecksumMalformado);
    EXPECT_EQ(analisa("$GNRMC,123519.00,A,1947.9,S*6", &t),
              ErroNmea::ChecksumMalformado);
}

TEST(Nmea, ChecksumErradoRecusaAntesDeInterpretar) {
    std::string s = kRmcGn;
    s[s.size() - 1] = s[s.size() - 1] == '0' ? '1' : '0';
    Telemetria t;
    EXPECT_EQ(analisa(s.c_str(), &t), ErroNmea::ChecksumInvalido);
}

TEST(Nmea, SentencaTruncadaNoMeio) {
    // Perda de bytes na UART: o que sobra pode ter checksum plausível por
    // acaso, mas não tem campos suficientes.
    const std::string s = com_checksum("$GNRMC,123519.00,A,1947.99496,S");
    Telemetria t;
    EXPECT_EQ(analisa(s.c_str(), &t), ErroNmea::CamposDeMenos);
}

TEST(Nmea, CamposComLixoSaoRecusados) {
    struct { const char* corpo; const char* porque; } casos[] = {
        {"$GNRMC,123519.00,A,ABCD.EFGH,S,04401.26264,W,1.0,90.0,220926,,,A",
         "latitude nao numerica"},
        {"$GNRMC,123519.00,A,1947.99496,X,04401.26264,W,1.0,90.0,220926,,,A",
         "hemisferio invalido"},
        {"$GNRMC,123519.00,A,1947.99496,S,04401.26264,W,-5.0,90.0,220926,,,A",
         "velocidade negativa"},
        {"$GNRMC,123519.00,A,1947.99496,S,04401.26264,W,1.0,400.0,220926,,,A",
         "rumo fora de 0-360"},
        {"$GNRMC,123519.00,A,9947.99496,S,04401.26264,W,1.0,90.0,220926,,,A",
         "latitude acima de 90"},
        {"$GNRMC,123519.00,A,1947.99496,S,04401.26264,W,1.2.3,90.0,220926,,,A",
         "dois pontos decimais"},
        {"$GNRMC,123519.00,A,,S,04401.26264,W,1.0,90.0,220926,,,A",
         "latitude vazia"},
    };
    for (const auto& c : casos) {
        const std::string s = com_checksum(c.corpo);
        Telemetria t;
        EXPECT_EQ(analisa(s.c_str(), &t), ErroNmea::CampoInvalido) << c.porque;
    }
}

TEST(Nmea, rumo_360_00_e_o_receptor_arredondando_359_996_e_vira_zero) {
    // O NEO-M8N escreve o rumo com duas casas. Um rumo de 359,996 sai como
    // "360.00" -- e recusar isso derrubava a RMC INTEIRA, posicao e
    // velocidade junto, por causa de um rumo que o receptor acabou de
    // arredondar para cima. 360 e 0 sao a mesma direcao.
    const std::string s = com_checksum(
        "$GNRMC,123519.00,A,1947.99496,S,04401.26264,W,1.0,360.00,220926,,,A");
    Telemetria t;
    ASSERT_EQ(analisa(s.c_str(), &t), ErroNmea::Nenhum);
    EXPECT_TRUE(t.rumo_valido);
    EXPECT_EQ(t.rumo_graus, 0.0F) << "360 tem de ser normalizado para 0";
    // E a posicao, que se perdia junto com o rumo, esta la: 19 47,99496' sul.
    EXPECT_NEAR(t.lat, -(19.0F + 47.99496F / 60.0F), 1e-4F);
}

TEST(Nmea, rumo_acima_de_360_continua_recusado) {
    for (const char* rumo : {"360.01", "361.0", "400.0"}) {
        const std::string s = com_checksum(
            std::string("$GNRMC,123519.00,A,1947.99496,S,04401.26264,W,1.0,") +
            rumo + ",220926,,,A");
        Telemetria t;
        EXPECT_EQ(analisa(s.c_str(), &t), ErroNmea::CampoInvalido) << rumo;
    }
}

TEST(Nmea, NenhumaEntradaCausaLeituraForaDoBuffer) {
    // Todo prefixo da sentença de referência, cada um COPIADO para um bloco
    // do tamanho exato: lido direto da `std::string`, o byte seguinte ainda
    // seria memória válida e o sanitizador não veria a leitura fora. E cada
    // prefixo incompleto tem de ser recusado sem tocar no destino -- só a
    // sentença inteira passa.
    const std::string base = kRmcGn;
    for (std::size_t n = 0; n <= base.size(); ++n) {
        const std::unique_ptr<char[]> exato(new char[n]);
        std::memcpy(exato.get(), base.data(), n);
        Telemetria t;
        t.lat = 1.0F;
        const ErroNmea e = analisa_rmc(exato.get(), n, &t);
        if (n == base.size()) {
            EXPECT_EQ(e, ErroNmea::Nenhum);
        } else {
            EXPECT_NE(e, ErroNmea::Nenhum) << "aceitou o prefixo de " << n;
            EXPECT_FLOAT_EQ(t.lat, 1.0F) << "escreveu no destino com " << n;
        }
    }
}

// ----------------------------------------- o que o checksum deixa passar --

TEST(Nmea, VelocidadeAbsurdaERecusadaEmVezDeVirarFloatIndefinido) {
    // O checksum protege contra ruido, nao contra um campo enorme bem
    // formado. Estreitar 1e50 nos para `float` e comportamento indefinido.
    // O NEO-M8N nao reporta acima de 500 m/s (~972 nos).
    for (const char* nos : {"100000000000000000000000000000000000000000000000000.0",
                            "1500.0"}) {
        const std::string s = com_checksum(
            std::string("$GNRMC,123519.00,A,1947.99496,S,04401.26264,W,") +
            nos + ",84.4,220926,,,A");
        Telemetria t;
        EXPECT_EQ(analisa(s.c_str(), &t), ErroNmea::CampoInvalido) << nos;
    }
}

TEST(Nmea, OPontoDaCoordenadaTemDeEstarDepoisDosDoisDigitosDeMinuto) {
    // Os minutos sao SEMPRE dois digitos antes do ponto. Sem exigir isso,
    // uma coluna deslocada vira uma posicao plausivel e errada.
    struct { const char* lat; const char* lon; } casos[] = {
        {"194.799496", "04401.26264"},   // lat com um digito de minuto
        {"1947.99496", "4401.26264"},    // lon com grau de dois digitos
        {"194799496", "04401.26264"},    // sem ponto nenhum
    };
    for (const auto& c : casos) {
        const std::string s = com_checksum(
            std::string("$GNRMC,123519.00,A,") + c.lat + ",S," + c.lon +
            ",W,22.4,84.4,220926,,,A");
        Telemetria t;
        EXPECT_EQ(analisa(s.c_str(), &t), ErroNmea::CampoInvalido)
            << c.lat << " " << c.lon;
    }
}

TEST(Nmea, NenhumCampoNumericoDaRmcAceitaSinal) {
    // A RMC diz o hemisferio por letra, e velocidade e rumo nao tem sinal.
    // Um `+` ou `-` ali so pode ser lixo que passou pelo checksum.
    const char* corpos[] = {
        "$GNRMC,123519.00,A,19+7.99496,S,04401.26264,W,22.4,84.4,220926,,,A",
        "$GNRMC,123519.00,A,1947.99496,S,04401.26264,W,+22.4,84.4,220926,,,A",
        "$GNRMC,123519.00,A,1947.99496,S,04401.26264,W,22.4,+84.4,220926,,,A",
        "$GNRMC,123519.00,A,1947.99496,S,04401.26264,W,-0.0,84.4,220926,,,A",
    };
    for (const char* c : corpos) {
        const std::string s = com_checksum(c);
        Telemetria t;
        EXPECT_EQ(analisa(s.c_str(), &t), ErroNmea::CampoInvalido) << c;
    }
}

TEST(Nmea, RmcQueTerminaNaDataEAceita) {
    // Variacao magnetica e modo vem DEPOIS da data e nao interessam. Exigir
    // o campo seguinte recusava uma RMC completa por falta de um opcional.
    const std::string s = com_checksum(
        "$GNRMC,123519.00,A,1947.99496,S,04401.26264,W,22.4,84.4,220926");
    Telemetria t;
    ASSERT_EQ(analisa(s.c_str(), &t), ErroNmea::Nenhum);
    EXPECT_TRUE(t.data_valida);
    EXPECT_EQ(t.ano, 2026);
}

TEST(Nmea, OSegundoBissextoNaoApagaAData) {
    // O `TempoUtc` aceita o 60 de proposito; a RMC o recusava e a fix do
    // segundo bissexto saia sem data nenhuma.
    const std::string s = com_checksum(
        "$GNRMC,235960.00,A,1947.99496,S,04401.26264,W,22.4,84.4,311226,,,A");
    Telemetria t;
    ASSERT_EQ(analisa(s.c_str(), &t), ErroNmea::Nenhum);
    EXPECT_TRUE(t.data_valida);
    EXPECT_EQ(t.segundo, 60);
}


// ------------------------------------------------- integracao c/ simulador --

TEST(Nmea, AceitaOQueOSimuladorProduz) {
    // Copiadas da saída de `simulador/simula_gps.py`, que percorre o Eixão com
    // geometria do OpenStreetMap. Amarra os dois lados: se o simulador mudar
    // de formato, este teste cai — e é melhor cair aqui que na bancada, com o
    // aparelho em silêncio e ninguém sabendo de quem é a culpa.
    struct { const char* frase; float kmh; bool tem_rumo; } casos[] = {
        {"$GNRMC,123519.00,A,1550.0330,S,04755.4453,W,0.00,,240926,,,A*53",
         0.0F, false},
        {"$GNRMC,123519.00,A,1550.0330,S,04755.4453,W,32.40,84.4,240926,,,A*70",
         60.0F, true},
        {"$GNRMC,123519.00,A,1550.0330,S,04755.4453,W,64.74,84.4,240926,,,A*74",
         119.9F, true},
    };
    for (const auto& c : casos) {
        Telemetria t;
        ASSERT_EQ(analisa(c.frase, &t), ErroNmea::Nenhum) << c.frase;
        EXPECT_NEAR(t.velocidade_kmh, c.kmh, 0.02F) << c.frase;
        EXPECT_EQ(t.rumo_valido, c.tem_rumo) << c.frase;
        // Eixão: por volta de 15,83 S e 47,92 W.
        EXPECT_NEAR(t.lat, -15.8339F, 0.001F);
        EXPECT_NEAR(t.lon, -47.9241F, 0.001F);
        EXPECT_TRUE(t.data_valida);
        EXPECT_EQ(t.dia, 24); EXPECT_EQ(t.mes, 9); EXPECT_EQ(t.ano, 2026);
    }
}

}  // namespace
