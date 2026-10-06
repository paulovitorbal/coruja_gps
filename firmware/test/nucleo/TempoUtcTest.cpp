#include "nucleo/TempoUtc.h"

#include <gtest/gtest.h>

#include <ctime>

namespace {

using namespace coruja;

TempoUtc t(int ano, int mes, int dia, int h = 0, int m = 0, int s = 0) {
    return {static_cast<std::uint16_t>(ano), static_cast<std::uint8_t>(mes),
            static_cast<std::uint8_t>(dia), static_cast<std::uint8_t>(h),
            static_cast<std::uint8_t>(m), static_cast<std::uint8_t>(s)};
}

// ===================================================== contra uma referência

TEST(TempoUtc, bate_com_timegm_em_datas_conhecidas) {
    // Verifica contra a biblioteca do sistema, e não contra números que eu
    // mesmo tivesse calculado -- é a diferença entre conferir a coisa e
    // conferir a minha conta sobre a coisa.
    struct Caso { int ano, mes, dia, h, m, s; };
    const Caso casos[] = {
        {1970, 1, 1, 0, 0, 0},       // a época
        {2000, 1, 1, 0, 0, 0},       // virada de século bissexto
        {2000, 2, 29, 12, 0, 0},     // 2000 É bissexto (divisível por 400)
        {1900, 3, 1, 0, 0, 0},       // 1900 NÃO é (divisível por 100)
        {2024, 2, 29, 23, 59, 59},
        {2026, 10, 6, 14, 30, 0},
        {2038, 1, 19, 3, 14, 7},     // o estouro do int32
        {2036, 2, 7, 6, 28, 16},     // a virada da era do NTP
        {2099, 12, 31, 23, 59, 59},
    };
    for (const auto& c : casos) {
        std::tm tm{};
        tm.tm_year = c.ano - 1900;
        tm.tm_mon = c.mes - 1;
        tm.tm_mday = c.dia;
        tm.tm_hour = c.h;
        tm.tm_min = c.m;
        tm.tm_sec = c.s;
        const auto esperado = static_cast<std::int64_t>(timegm(&tm));
        EXPECT_EQ(segundos_de(t(c.ano, c.mes, c.dia, c.h, c.m, c.s)), esperado)
            << c.ano << "-" << c.mes << "-" << c.dia;
    }
}

TEST(TempoUtc, a_volta_devolve_o_mesmo_instante) {
    // Ida e volta em todo dia 1 e todo dia 28 de 1970 a 2100: se bissexto ou
    // virada de século estiverem errados em qualquer direção, algum par não
    // fecha.
    for (int ano = 1970; ano <= 2100; ++ano) {
        for (int mes = 1; mes <= 12; ++mes) {
            for (int dia : {1, 28}) {
                const TempoUtc antes = t(ano, mes, dia, 13, 45, 7);
                const TempoUtc depois = de_segundos(segundos_de(antes));
                EXPECT_EQ(depois.ano, antes.ano);
                EXPECT_EQ(depois.mes, antes.mes);
                EXPECT_EQ(depois.dia, antes.dia);
                EXPECT_EQ(depois.hora, 13);
                EXPECT_EQ(depois.minuto, 45);
                EXPECT_EQ(depois.segundo, 7);
            }
        }
    }
}

TEST(TempoUtc, o_dia_seguinte_fica_86400_segundos_adiante) {
    EXPECT_EQ(segundos_de(t(2026, 10, 7)) - segundos_de(t(2026, 10, 6)), 86400);
    // Virada de ano, de mês e de bissexto.
    EXPECT_EQ(segundos_de(t(2027, 1, 1)) - segundos_de(t(2026, 12, 31)), 86400);
    EXPECT_EQ(segundos_de(t(2024, 3, 1)) - segundos_de(t(2024, 2, 29)), 86400);
    EXPECT_EQ(segundos_de(t(2025, 3, 1)) - segundos_de(t(2025, 2, 28)), 86400);
}

// ================================================= antes da época e negativos

TEST(TempoUtc, antes_de_1970_a_divisao_arredonda_para_baixo) {
    // `/` em C++ trunca na direção do zero. Sem corrigir, 1969-12-31 23:00
    // viraria 1970-01-01 com hora negativa -- e o campo é sem sinal.
    const TempoUtc v = de_segundos(-3600);
    EXPECT_EQ(v.ano, 1969);
    EXPECT_EQ(v.mes, 12);
    EXPECT_EQ(v.dia, 31);
    EXPECT_EQ(v.hora, 23);
    EXPECT_EQ(v.minuto, 0);
}

TEST(TempoUtc, um_segundo_antes_da_epoca) {
    const TempoUtc v = de_segundos(-1);
    EXPECT_EQ(v.ano, 1969);
    EXPECT_EQ(v.mes, 12);
    EXPECT_EQ(v.dia, 31);
    EXPECT_EQ(v.hora, 23);
    EXPECT_EQ(v.minuto, 59);
    EXPECT_EQ(v.segundo, 59);
}

TEST(TempoUtc, valor_absurdo_devolve_zerado_e_nao_lixo_plausivel) {
    for (std::int64_t s : {std::int64_t{1} << 60, -(std::int64_t{1} << 60),
                           std::int64_t{1} << 50}) {
        EXPECT_FALSE(de_segundos(s).plausivel()) << s;
    }
}

TEST(TempoUtc, dias_que_truncam_para_uma_data_BOA_sao_recusados) {
    // O pior caso possível, e o que os valores redondos do teste acima NÃO
    // alcançam: 371.086.965.619.200 s dá 4.294.988.028 dias, que truncado
    // em 32 bits vira 20.732 -- exatamente 2026-10-06. Sem a guarda de
    // faixa, uma entrada absurda produziria uma data perfeitamente
    // plausível, e com TLS uma data plausível e errada aceita certificado
    // vencido.
    const std::int64_t armadilha = 371086965619200LL;
    const TempoUtc v = de_segundos(armadilha);
    EXPECT_FALSE(v.plausivel())
        << v.ano << "-" << int(v.mes) << "-" << int(v.dia);
    EXPECT_NE(v.ano, 2026);
}

TEST(TempoUtc, ano_que_trunca_para_dentro_da_faixa_e_recusado) {
    // 2.070.655.228.800 s cai no ano 67.586, cujos 16 bits de baixo são
    // 2050. O campo é `uint16_t`: sem a guarda, o ano sairia como 2050 e
    // passaria no `plausivel()`.
    const std::int64_t armadilha = 2070655228800LL;
    const TempoUtc v = de_segundos(armadilha);
    EXPECT_FALSE(v.plausivel()) << v.ano;
    EXPECT_NE(v.ano, 2050);
}

// ============================================================== plausível

TEST(TempoUtc, relogio_zerado_nao_e_plausivel) {
    // É o caso que importa: o RP2350 não tem bateria no relógio e todo boot
    // começa em zero. Zero vira 1970, e 1970 tem de ser recusado.
    EXPECT_FALSE(de_segundos(0).plausivel());
    EXPECT_FALSE(TempoUtc{}.plausivel());
}

TEST(TempoUtc, a_faixa_do_aparelho_e_aceita) {
    EXPECT_TRUE(t(2020, 1, 1).plausivel());
    EXPECT_TRUE(t(2026, 10, 6, 14, 30, 0).plausivel());
    EXPECT_TRUE(t(2099, 12, 31, 23, 59, 59).plausivel());
}

TEST(TempoUtc, fora_da_faixa_nao_e) {
    EXPECT_FALSE(t(2019, 12, 31).plausivel());
    EXPECT_FALSE(t(2100, 1, 1).plausivel());
    EXPECT_FALSE(t(2026, 13, 1).plausivel());
    EXPECT_FALSE(t(2026, 0, 1).plausivel());
    EXPECT_FALSE(t(2026, 10, 0).plausivel());
    EXPECT_FALSE(t(2026, 10, 32).plausivel());
    EXPECT_FALSE(t(2026, 10, 6, 24, 0, 0).plausivel());
    EXPECT_FALSE(t(2026, 10, 6, 0, 60, 0).plausivel());
}

TEST(TempoUtc, o_segundo_bissexto_passa) {
    // NMEA e NTP podem entregar 60. Recusar a hora inteira por causa dele
    // seria perder um segundo por ano em troca de preciosismo.
    EXPECT_TRUE(t(2026, 12, 31, 23, 59, 60).plausivel());
}

// ============================================================ dias e civil

TEST(DiasDesdeEpoca, a_epoca_e_o_dia_zero) {
    EXPECT_EQ(dias_desde_epoca(1970, 1, 1), 0);
    EXPECT_EQ(dias_desde_epoca(1970, 1, 2), 1);
    EXPECT_EQ(dias_desde_epoca(1969, 12, 31), -1);
}

TEST(CivilDeDias, e_o_inverso_exato) {
    for (std::int32_t d = -40000; d <= 60000; d += 7) {
        int ano = 0;
        unsigned mes = 0, dia = 0;
        civil_de_dias(d, &ano, &mes, &dia);
        EXPECT_EQ(dias_desde_epoca(ano, mes, dia), d) << d;
    }
}

TEST(CivilDeDias, aceita_ponteiros_nulos) {
    int ano = 0;
    civil_de_dias(0, &ano, nullptr, nullptr);
    EXPECT_EQ(ano, 1970);
    civil_de_dias(0, nullptr, nullptr, nullptr);  // não pode explodir
}

}  // namespace
