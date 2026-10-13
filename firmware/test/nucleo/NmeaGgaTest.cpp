#include "nucleo/Nmea.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

using namespace coruja;

/// Checksum calculado, nunca suposto — a mesma disciplina do `NmeaTest`.
std::string com_checksum(const std::string& corpo) {
    std::uint8_t cs = 0;
    for (std::size_t i = 1; i < corpo.size(); ++i) {
        cs ^= static_cast<std::uint8_t>(corpo[i]);
    }
    char fim[8];
    std::snprintf(fim, sizeof fim, "*%02X", cs);
    return corpo + fim;
}

/// Uma GGA do NEO-M8N: mesmo instante das RMC de referência do `NmeaTest`.
///
/// Campos: hora, lat, N/S, lon, E/W, qualidade, satélites, HDOP, altitude,
/// unidade, separação geoidal, unidade, idade do DGPS, estação.
std::string gga(const std::string& qualidade = "1",
                const std::string& satelites = "09",
                const std::string& hdop = "0.92",
                const std::string& talker = "GN") {
    return com_checksum(
        "$" + talker + "GGA,123519.00,1947.99496,S,04401.26264,W," +
        qualidade + "," + satelites + "," + hdop + ",545.4,M,46.9,M,,");
}

ErroNmea analisa(const std::string& s, QualidadeFix* q) {
    return analisa_gga(s.c_str(), s.size(), q);
}

// ------------------------------------------------------- o caso que serve --

TEST(NmeaGga, extrai_satelites_e_hdop) {
    QualidadeFix q;
    ASSERT_EQ(analisa(gga(), &q), ErroNmea::Nenhum);
    EXPECT_EQ(q.satelites, 9);
    EXPECT_NEAR(q.hdop, 0.92F, 1e-4F);
}

TEST(NmeaGga, o_hdop_e_geometria_e_NAO_se_deduz_dos_satelites) {
    // Doze satélites amontoados num canto do céu dão HDOP ruim; cinco bem
    // espalhados dão HDOP bom. São números independentes, e é por isso que
    // o campo e lido, nao estimado a partir da contagem.
    QualidadeFix muitos;
    ASSERT_EQ(analisa(gga("1", "12", "4.80"), &muitos), ErroNmea::Nenhum);
    QualidadeFix poucos;
    ASSERT_EQ(analisa(gga("1", "05", "1.10"), &poucos), ErroNmea::Nenhum);
    EXPECT_GT(muitos.satelites, poucos.satelites);
    EXPECT_GT(muitos.hdop, poucos.hdop);      // mais satelites, geometria PIOR
}

TEST(NmeaGga, aceita_qualquer_talker_como_a_RMC) {
    // Mesma razão do RF01.1: o talker muda com o modo GNSS, e um parser
    // fixado em `GN` não casaria nada em modo GPS-only.
    for (const char* t : {"GN", "GP", "GL", "GA"}) {
        QualidadeFix q;
        EXPECT_EQ(analisa(gga("1", "09", "0.92", t), &q), ErroNmea::Nenhum)
            << "talker " << t;
    }
}

TEST(NmeaGga, hdop_inteiro_sem_casas_decimais) {
    QualidadeFix q;
    ASSERT_EQ(analisa(gga("1", "07", "2"), &q), ErroNmea::Nenhum);
    EXPECT_NEAR(q.hdop, 2.0F, 1e-4F);
}

TEST(NmeaGga, hdop_grande_de_ceu_ruim) {
    QualidadeFix q;
    ASSERT_EQ(analisa(gga("1", "04", "25.4"), &q), ErroNmea::Nenhum);
    EXPECT_NEAR(q.hdop, 25.4F, 1e-3F);
}

// ------------------------------------------------------------- recusas ----

TEST(NmeaGga, qualidade_zero_e_sem_fix_e_nao_erro_de_formato) {
    // Sentença íntegra, dado ausente: o receptor está dizendo que ainda não
    // sabe onde está. Tratar como erro de formato mandaria procurar no lugar
    // errado.
    QualidadeFix q;
    EXPECT_EQ(analisa(gga("0", "00", "99.99"), &q), ErroNmea::SemFix);
}

TEST(NmeaGga, sem_fix_NAO_escreve_no_destino) {
    // Mesma garantia da RMC: uma sentença sem fix não pode apagar a última
    // qualidade conhecida.
    QualidadeFix q;
    q.satelites = 11;
    q.hdop = 1.2F;
    analisa(gga("0", "00", "99.99"), &q);
    EXPECT_EQ(q.satelites, 11);
    EXPECT_NEAR(q.hdop, 1.2F, 1e-4F);
}

TEST(NmeaGga, uma_RMC_nao_e_GGA) {
    QualidadeFix q;
    const std::string rmc = com_checksum(
        "$GNRMC,123519.00,A,1947.99496,S,04401.26264,W,22.4,84.4,220926,,,A");
    EXPECT_EQ(analisa(rmc, &q), ErroNmea::NaoEhGga);
}

TEST(NmeaGga, checksum_errado_e_recusado) {
    QualidadeFix q;
    std::string s = gga();
    s[s.size() - 1] = (s[s.size() - 1] == '0') ? '1' : '0';
    EXPECT_EQ(analisa(s, &q), ErroNmea::ChecksumInvalido);
}

TEST(NmeaGga, sentenca_truncada_e_campos_de_menos) {
    QualidadeFix q;
    EXPECT_EQ(analisa(com_checksum("$GNGGA,123519.00,1947.99496,S"), &q),
              ErroNmea::CamposDeMenos);
}

TEST(NmeaGga, hdop_vazio_e_campo_invalido) {
    // O M8N emite o campo vazio quando ainda não tem solução. Aceitar como
    // zero seria afirmar geometria PERFEITA justamente quando não há nenhuma.
    QualidadeFix q;
    EXPECT_EQ(analisa(gga("1", "09", ""), &q), ErroNmea::CampoInvalido);
}

TEST(NmeaGga, satelites_vazio_e_campo_invalido) {
    QualidadeFix q;
    EXPECT_EQ(analisa(gga("1", "", "0.92"), &q), ErroNmea::CampoInvalido);
}

TEST(NmeaGga, hdop_nao_numerico_e_campo_invalido) {
    QualidadeFix q;
    EXPECT_EQ(analisa(gga("1", "09", "abc"), &q), ErroNmea::CampoInvalido);
}

TEST(NmeaGga, hdop_negativo_e_campo_invalido) {
    // Não existe diluição de precisão negativa.
    QualidadeFix q;
    EXPECT_EQ(analisa(gga("1", "09", "-1.5"), &q), ErroNmea::CampoInvalido);
}

TEST(NmeaGga, contagem_de_satelites_absurda_e_campo_invalido) {
    // O M8N rastreia dezenas, não centenas. Um valor assim é lixo que passou
    // pelo checksum por acaso, ou campo deslocado.
    QualidadeFix q;
    EXPECT_EQ(analisa(gga("1", "99", "0.92"), &q), ErroNmea::CampoInvalido);
}

TEST(NmeaGga, vazia_e_vazia) {
    QualidadeFix q;
    EXPECT_EQ(analisa_gga(nullptr, 0, &q), ErroNmea::Vazia);
    EXPECT_EQ(analisa_gga("", 0, &q), ErroNmea::Vazia);
}

TEST(NmeaGga, destino_nulo_nao_estoura) {
    const std::string s = gga();
    EXPECT_EQ(analisa_gga(s.c_str(), s.size(), nullptr), ErroNmea::Vazia);
}

TEST(NmeaGga, descricao_do_erro_novo_existe) {
    EXPECT_STRNE(descreve(ErroNmea::NaoEhGga), nullptr);
    EXPECT_STRNE(descreve(ErroNmea::NaoEhGga), descreve(ErroNmea::NaoEhRmc));
}

}  // namespace
