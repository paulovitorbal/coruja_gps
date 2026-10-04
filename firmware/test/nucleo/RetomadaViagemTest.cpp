#include "nucleo/RetomadaViagem.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace {

using namespace coruja;

Telemetria quando(std::uint16_t ano, std::uint8_t mes, std::uint8_t dia,
                  std::uint8_t hora, std::uint8_t minuto) {
    Telemetria t;
    t.ano = ano; t.mes = mes; t.dia = dia;
    t.hora = hora; t.minuto = minuto;
    t.data_valida = true;
    return t;
}

EstadoViagemSalvo salvo(std::uint16_t ano, std::uint8_t mes, std::uint8_t dia,
                        std::uint8_t hora, std::uint8_t minuto,
                        float dist = 12.34F) {
    EstadoViagemSalvo e;
    e.ativa = true;
    e.ano = ano; e.mes = mes; e.dia = dia;
    e.hora = hora; e.minuto = minuto;
    e.dist_km = dist;
    return e;
}

// --- a regua de tempo: referencias calculadas em Python ---

TEST(RetomadaViagem, minutos_utc_bate_com_a_referencia) {
    EXPECT_EQ(minutos_utc(1970, 1, 1, 0, 0), 0);
    EXPECT_EQ(minutos_utc(2026, 10, 4, 17, 30), 29852250);
    EXPECT_EQ(minutos_utc(2026, 10, 4, 23, 59), 29852639);
    EXPECT_EQ(minutos_utc(2026, 10, 5, 0, 1), 29852641);
    EXPECT_EQ(minutos_utc(2026, 12, 31, 23, 30), 29979330);
    EXPECT_EQ(minutos_utc(2027, 1, 1, 1, 0), 29979420);
    EXPECT_EQ(minutos_utc(2024, 2, 29, 12, 0), 28486800) << "29 de fevereiro";
    EXPECT_EQ(minutos_utc(2026, 3, 1, 0, 0), 29538720);
}

TEST(RetomadaViagem, a_diferenca_atravessa_a_virada_do_dia) {
    // Subtrair campo a campo daria -1438 minutos em vez de 2.
    EXPECT_EQ(minutos_utc(2026, 10, 5, 0, 1) - minutos_utc(2026, 10, 4, 23, 59), 2);
}

TEST(RetomadaViagem, a_diferenca_atravessa_a_virada_do_ano) {
    EXPECT_EQ(minutos_utc(2027, 1, 1, 1, 0) - minutos_utc(2026, 12, 31, 23, 30), 90);
}

// --- a janela de 120 minutos ---

TEST(RetomadaViagem, parada_curta_retoma) {
    EXPECT_TRUE(pode_retomar(salvo(2026, 10, 4, 12, 0),
                             quando(2026, 10, 4, 13, 20)));   // 80 min
}

TEST(RetomadaViagem, o_almoco_de_estrada_retoma) {
    // O caso que define o numero: parar para comer e voltar e a mesma viagem.
    EXPECT_TRUE(pode_retomar(salvo(2026, 10, 4, 11, 45),
                             quando(2026, 10, 4, 13, 30)));   // 105 min
}

TEST(RetomadaViagem, a_janela_e_inclusiva_em_120_minutos) {
    EXPECT_TRUE(pode_retomar(salvo(2026, 10, 4, 12, 0),
                            quando(2026, 10, 4, 14, 0)));     // 120 exatos
    EXPECT_FALSE(pode_retomar(salvo(2026, 10, 4, 12, 0),
                             quando(2026, 10, 4, 14, 1)));    // 121
}

TEST(RetomadaViagem, dormir_e_sair_no_dia_seguinte_nao_retoma) {
    EXPECT_FALSE(pode_retomar(salvo(2026, 10, 4, 19, 0),
                              quando(2026, 10, 5, 7, 30)));
}

TEST(RetomadaViagem, a_janela_funciona_atravessando_a_meia_noite) {
    EXPECT_TRUE(pode_retomar(salvo(2026, 10, 4, 23, 30),
                            quando(2026, 10, 5, 0, 45)));     // 75 min
}

// --- as recusas ---

TEST(RetomadaViagem, viagem_inativa_nao_retoma) {
    auto e = salvo(2026, 10, 4, 12, 0);
    e.ativa = false;
    EXPECT_FALSE(pode_retomar(e, quando(2026, 10, 4, 12, 10)));
}

TEST(RetomadaViagem, sem_data_no_fix_nao_retoma) {
    auto t = quando(2026, 10, 4, 12, 10);
    t.data_valida = false;
    EXPECT_FALSE(pode_retomar(salvo(2026, 10, 4, 12, 0), t));
}

TEST(RetomadaViagem, relogio_para_tras_nao_retoma) {
    // Dado corrompido, ou o cartao de outro aparelho. Retomar sobre isso
    // continuaria uma distancia que nao se sabe de onde veio.
    EXPECT_FALSE(pode_retomar(salvo(2026, 10, 4, 14, 0),
                              quando(2026, 10, 4, 13, 0)));
}

TEST(RetomadaViagem, data_salva_implausivel_nao_retoma) {
    EXPECT_FALSE(pode_retomar(salvo(1999, 10, 4, 12, 0),
                              quando(2026, 10, 4, 12, 10)));
    EXPECT_FALSE(pode_retomar(salvo(2026, 13, 4, 12, 0),
                              quando(2026, 10, 4, 12, 10)));
}

// --- serializacao ---

TEST(RetomadaViagem, o_estado_vai_e_volta_inteiro) {
    const auto e = salvo(2026, 10, 4, 17, 31, 180.45F);
    char buf[kTamEstadoViagem];
    const auto n = formata_estado(e, buf, sizeof buf);
    ASSERT_GT(n, 0u);

    EstadoViagemSalvo lido;
    ASSERT_TRUE(analisa_estado(buf, n, &lido));
    EXPECT_TRUE(lido.ativa);
    EXPECT_EQ(lido.ano, 2026);
    EXPECT_EQ(lido.mes, 10);
    EXPECT_EQ(lido.dia, 4);
    EXPECT_EQ(lido.hora, 17);
    EXPECT_EQ(lido.minuto, 31);
    EXPECT_NEAR(lido.dist_km, 180.45F, 0.005F);
}

TEST(RetomadaViagem, o_arquivo_e_legivel_a_olho) {
    char buf[kTamEstadoViagem];
    formata_estado(salvo(2026, 10, 4, 17, 31, 180.45F), buf, sizeof buf);
    EXPECT_STREQ(buf, "ativa=1\nutc=2026-10-04T17:31Z\ndist_km=180.45\n");
}

TEST(RetomadaViagem, texto_truncado_ou_sujo_e_recusado) {
    EstadoViagemSalvo lido;
    const char* sujos[] = {
        "",
        "ativa=1\n",
        "ativa=1\nutc=2026-10-04T17:31Z\n",      // sem distancia
        "lixo qualquer",
        "ativa=1\nutc=0000-00-00T00:00Z\ndist_km=1.0\n",  // data impossivel
    };
    for (const char* s : sujos) {
        EXPECT_FALSE(analisa_estado(s, std::strlen(s), &lido))
            << "aceitou: " << s;
    }
}

TEST(RetomadaViagem, entrada_maior_que_o_buffer_e_recusada) {
    EstadoViagemSalvo lido;
    char grande[kTamEstadoViagem * 2] = {};
    std::memset(grande, 'a', sizeof grande - 1);
    EXPECT_FALSE(analisa_estado(grande, sizeof grande - 1, &lido));
}

TEST(RetomadaViagem, ponteiros_nulos_nao_quebram) {
    EstadoViagemSalvo lido;
    EXPECT_FALSE(analisa_estado(nullptr, 10, &lido));
    EXPECT_FALSE(analisa_estado("ativa=1\n", 8, nullptr));
    char c[1];
    EXPECT_EQ(formata_estado(salvo(2026, 10, 4, 12, 0), nullptr, 10), 0u);
    EXPECT_EQ(formata_estado(salvo(2026, 10, 4, 12, 0), c, 0), 0u);
}

}  // namespace
