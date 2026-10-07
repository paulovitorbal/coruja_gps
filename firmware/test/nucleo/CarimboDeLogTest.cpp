#include "nucleo/CarimboDeLog.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "nucleo/TempoUtc.h"

namespace {

using namespace coruja;

std::string carimbo(std::int64_t utc_s, std::uint32_t ms = 0) {
    char buf[kTamCarimbo] = {};
    formata_carimbo(utc_s, ms, buf, sizeof buf);
    return buf;
}

TEST(CarimboDeLog, relogio_acertado_sai_em_iso8601) {
    // O valor vem de `segundos_de`, e não de um número escrito à mão: um
    // literal errado faria o teste concordar com um formatador errado.
    const std::int64_t s = segundos_de(TempoUtc{2026, 10, 7, 14, 41, 22});
    EXPECT_EQ(carimbo(s), "2026-10-07T14:41:22Z");
}

TEST(CarimboDeLog, relogio_zerado_sai_como_intervalo) {
    // Zero é exatamente o que `hora_utc()` devolve enquanto ninguém acertou
    // o relógio. Inventar 1970 aqui faria alguém ler como medição.
    EXPECT_EQ(carimbo(0, 12345), "+00012345");
}

TEST(CarimboDeLog, as_duas_formas_nao_se_confundem) {
    // A propriedade da qual uma ferramenta de análise depende: dá para
    // separar as duas por um único caractere, sem ambiguidade.
    EXPECT_EQ(carimbo(0, 1)[0], '+');
    EXPECT_NE(carimbo(segundos_de(TempoUtc{2026, 1, 1, 0, 0, 0}))[0], '+');
}

TEST(CarimboDeLog, data_implausivel_tambem_vira_intervalo) {
    // Não basta tratar o zero. Um relógio que foi para 1999 ou para 2200
    // não mediu nada -- e uma data com cara de data engana mais que um
    // intervalo honesto.
    EXPECT_EQ(carimbo(segundos_de(TempoUtc{1999, 6, 1, 0, 0, 0}), 7)[0], '+');
    EXPECT_EQ(carimbo(segundos_de(TempoUtc{2200, 6, 1, 0, 0, 0}), 7)[0], '+');
    // E a borda de baixo que o `plausivel()` aceita segue sendo data.
    EXPECT_NE(carimbo(segundos_de(TempoUtc{2020, 1, 1, 0, 0, 0}))[0], '+');
}

TEST(CarimboDeLog, tempo_antes_da_epoca_vira_intervalo) {
    EXPECT_EQ(carimbo(-1, 42)[0], '+');
}

TEST(CarimboDeLog, o_carimbo_cabe_exatamente_no_tamanho_declarado) {
    // `kTamCarimbo` é o que os chamadores reservam. Se a forma mais longa
    // passasse disso, a linha sairia truncada no cartão -- em silêncio.
    const std::int64_t s = segundos_de(TempoUtc{2099, 12, 31, 23, 59, 59});
    EXPECT_EQ(carimbo(s).size(), kTamCarimbo - 1);
    EXPECT_EQ(carimbo(s), "2099-12-31T23:59:59Z");
}

TEST(CarimboDeLog, ms_grande_nao_e_truncado) {
    // Oito dígitos cobrem 27 h de ligação contínua. Passando disso o campo
    // cresce, e perder alinhamento é melhor do que mentir sobre o tempo.
    EXPECT_EQ(carimbo(0, 123456789u), "+123456789");
}

TEST(CarimboDeLog, buffer_pequeno_devolve_vazio_em_vez_de_meia_data) {
    // Meia data é pior que nada: `2026-10-0` parece uma data e não é.
    char curto[8] = "LIXO!!";
    formata_carimbo(segundos_de(TempoUtc{2026, 10, 7, 14, 41, 22}), 0,
                    curto, sizeof curto);
    EXPECT_STREQ(curto, "");
}

TEST(CarimboDeLog, destino_nulo_nao_estoura) {
    formata_carimbo(0, 0, nullptr, 10);
    formata_carimbo(0, 0, nullptr, 0);
}

}  // namespace
