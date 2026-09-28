#include "nucleo/PeriodoDoDia.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

constexpr float kLatBsb = -15.79F;
constexpr float kLonBsb = -47.88F;

/// Valores de referência calculados **fora deste código**, por uma
/// implementação independente do algoritmo do NOAA em Python. Não são a
/// saída do `crepusculo()` copiada de volta: se fossem, o teste concordaria
/// com qualquer erro consistente — a mesma disciplina dos quadros UBX, e o
/// oposto do que aconteceu com os checksums NMEA escritos à mão.
///
/// Conferidos por um caminho terceiro: no equinócio o dia dá 12h06m e o
/// meio-dia solar cai às 12:18 local, que é a soma da correção de longitude
/// (Brasília está 2,88° a oeste do meridiano de UTC−3, ou +11,5 min) com a
/// equação do tempo de março (+7,4 min).
struct Referencia { int ano, mes, dia, nascer, por; };
constexpr Referencia kBrasilia[] = {
    {2026,  3, 21,  556, 1281},   // equinócio de março
    {2026,  6, 21,  578, 1249},   // solstício de junho — o dia mais curto
    {2026,  9, 28,  536, 1268},
    {2026, 12, 21,  518, 1302},   // solstício de dezembro — o mais longo
};

Telemetria em(int ano, int mes, int dia, int hora, int minuto,
              float lat = kLatBsb, float lon = kLonBsb) {
    Telemetria t;
    t.data_valida = true;
    t.ano = static_cast<std::uint16_t>(ano);
    t.mes = static_cast<std::uint8_t>(mes);
    t.dia = static_cast<std::uint8_t>(dia);
    t.hora = static_cast<std::uint8_t>(hora);
    t.minuto = static_cast<std::uint8_t>(minuto);
    t.lat = lat;
    t.lon = lon;
    return t;
}

// ================================================ contra a referência externa

TEST(Crepusculo, bate_com_a_referencia_do_NOAA_em_brasilia) {
    for (const auto& r : kBrasilia) {
        const Crepusculo c = crepusculo(r.ano, r.mes, r.dia, kLatBsb, kLonBsb);
        ASSERT_TRUE(c.valido) << r.dia << "/" << r.mes;
        // Dois minutos de tolerância: é `float` numa conta que a referência
        // faz em `double`, e o modo dia/noite não precisa de mais que isso.
        EXPECT_NEAR(c.nascer_min, r.nascer, 2) << "nascer em " << r.dia << "/" << r.mes;
        EXPECT_NEAR(c.por_min, r.por, 2) << "por em " << r.dia << "/" << r.mes;
    }
}

TEST(Crepusculo, o_dia_e_mais_curto_no_solsticio_de_junho_no_hemisferio_sul) {
    // Verificação por um caminho independente da referência: a ORDEM das
    // durações não depende da precisão da conta, só do sinal da declinação.
    // Se alguém trocar esse sinal, os números continuam plausíveis e esta
    // asserção é que quebra.
    const Crepusculo jun = crepusculo(2026, 6, 21, kLatBsb, kLonBsb);
    const Crepusculo dez = crepusculo(2026, 12, 21, kLatBsb, kLonBsb);
    EXPECT_LT(jun.por_min - jun.nascer_min, dez.por_min - dez.nascer_min)
        << "junho ficou mais longo que dezembro: declinacao com sinal trocado";
}

TEST(Crepusculo, o_equinocio_da_pouco_mais_de_doze_horas) {
    // Pouco MAIS, e não exatas: o zênite de 90,833° inclui a refração e meio
    // disco solar. Exatamente 12 h denunciaria que essa correção sumiu.
    const Crepusculo c = crepusculo(2026, 3, 21, kLatBsb, kLonBsb);
    const int duracao = c.por_min - c.nascer_min;
    EXPECT_GT(duracao, 12 * 60);
    EXPECT_LT(duracao, 12 * 60 + 15);
}

TEST(Crepusculo, a_longitude_desloca_o_horario_em_UTC) {
    // Quatro minutos por grau. Se a longitude fosse ignorada, dois pontos a
    // 15° de distancia dariam o mesmo horario UTC.
    const Crepusculo oeste = crepusculo(2026, 3, 21, kLatBsb, -60.0F);
    const Crepusculo leste = crepusculo(2026, 3, 21, kLatBsb, -45.0F);
    EXPECT_NEAR(oeste.nascer_min - leste.nascer_min, 60, 3);
}

// ====================================================== casos extremos

TEST(Crepusculo, noite_polar_nao_produz_conta_invalida) {
    // acos fora de [-1,1] daria NaN, e NaN comparado a qualquer coisa e
    // falso -- o aparelho decidiria "dia" em plena noite polar.
    //
    // Solsticio de JUNHO, com o sol sobre o tropico NORTE: quem fica no
    // escuro e o sul. (Escrevi este teste com os hemisferios trocados na
    // primeira versao; o codigo e que estava certo.)
    const Crepusculo c = crepusculo(2026, 6, 21, -80.0F, 0.0F);
    EXPECT_FALSE(c.valido);
    EXPECT_FALSE(c.sol_sempre_acima);
    EXPECT_EQ(periodo_do_dia(em(2026, 6, 21, 12, 0, -80.0F, 0.0F)),
              PeriodoDoDia::Noite);
}

TEST(Crepusculo, sol_da_meia_noite_tambem) {
    const Crepusculo c = crepusculo(2026, 6, 21, 80.0F, 0.0F);
    EXPECT_FALSE(c.valido);
    EXPECT_TRUE(c.sol_sempre_acima);
    EXPECT_EQ(periodo_do_dia(em(2026, 6, 21, 0, 0, 80.0F, 0.0F)),
              PeriodoDoDia::Dia);
}

TEST(Crepusculo, os_horarios_ficam_sempre_dentro_do_dia) {
    // Em longitudes a leste o meio-dia solar cai perto de 00:00 UTC e a
    // conta crua devolve minuto NEGATIVO para o nascer.
    for (const float lon : {-175.0F, -90.0F, 0.0F, 90.0F, 175.0F}) {
        const Crepusculo c = crepusculo(2026, 3, 21, -20.0F, lon);
        ASSERT_TRUE(c.valido) << "lon " << lon;
        EXPECT_GE(c.nascer_min, 0) << "lon " << lon;
        EXPECT_LT(c.nascer_min, 1440) << "lon " << lon;
        EXPECT_GE(c.por_min, 0) << "lon " << lon;
        EXPECT_LT(c.por_min, 1440) << "lon " << lon;
    }
}

// ========================================================= o período

TEST(PeriodoDoDia, sem_data_nao_se_chuta) {
    // Sem RTC, o aparelho passa ate 26 s sem data no boot. Chutar faria o
    // brilho mudar sozinho no arranque e voltar ao primeiro fix.
    Telemetria t;
    EXPECT_EQ(periodo_do_dia(t), PeriodoDoDia::Desconhecido);
}

TEST(PeriodoDoDia, meio_dia_em_brasilia_e_dia) {
    EXPECT_EQ(periodo_do_dia(em(2026, 9, 28, 15, 0)), PeriodoDoDia::Dia);
}

TEST(PeriodoDoDia, madrugada_em_brasilia_e_noite) {
    EXPECT_EQ(periodo_do_dia(em(2026, 9, 28, 4, 0)), PeriodoDoDia::Noite);
}

TEST(PeriodoDoDia, logo_depois_do_por_do_sol_ja_e_noite) {
    // 28/09 o sol se poe as 1268 min UTC = 21:08. Um minuto antes e depois.
    EXPECT_EQ(periodo_do_dia(em(2026, 9, 28, 21, 0)), PeriodoDoDia::Dia);
    EXPECT_EQ(periodo_do_dia(em(2026, 9, 28, 21, 30)), PeriodoDoDia::Noite);
}

TEST(PeriodoDoDia, logo_antes_do_nascer_ainda_e_noite) {
    // Nascer as 536 min UTC = 08:56.
    EXPECT_EQ(periodo_do_dia(em(2026, 9, 28, 8, 30)), PeriodoDoDia::Noite);
    EXPECT_EQ(periodo_do_dia(em(2026, 9, 28, 9, 30)), PeriodoDoDia::Dia);
}

TEST(PeriodoDoDia, funciona_onde_o_dia_cruza_a_meia_noite_UTC) {
    // Em Brasilia o intervalo de dia nao cruza a meia-noite UTC, entao o
    // caso do `else` so aparece longe daqui. Nova Zelandia (175°E): o dia
    // local comeca antes da meia-noite UTC e termina depois.
    const Crepusculo c = crepusculo(2026, 3, 21, -41.0F, 175.0F);
    ASSERT_TRUE(c.valido);
    EXPECT_GT(c.nascer_min, c.por_min) << "este teste perdeu o sentido";
    EXPECT_EQ(periodo_do_dia(em(2026, 3, 21, 23, 0, -41.0F, 175.0F)),
              PeriodoDoDia::Dia);
    EXPECT_EQ(periodo_do_dia(em(2026, 3, 21, 12, 0, -41.0F, 175.0F)),
              PeriodoDoDia::Noite);
}

TEST(PeriodoDoDia, descricoes_cobrem_todos_os_valores) {
    for (const auto p : {PeriodoDoDia::Desconhecido, PeriodoDoDia::Dia,
                         PeriodoDoDia::Noite}) {
        EXPECT_STRNE(descreve(p), "?");
    }
}

}  // namespace
