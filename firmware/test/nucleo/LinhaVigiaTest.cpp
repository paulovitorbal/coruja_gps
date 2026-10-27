#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "nucleo/LinhaVigia.h"

namespace {

coruja::EstadoVigia completo() {
    coruja::EstadoVigia e;
    e.pontos_base = 18367;
    e.tem_fix = true;
    e.zona = 1;
    e.tem_qualidade = true;
    e.hdop = 0.7F;
    e.satelites = 12;
    e.tem_temperatura = true;
    e.temperatura_c = 26.6F;
    e.serial_perdidos = 0;
    e.pior_volta_ms = 268;
    return e;
}

std::string formata(const coruja::EstadoVigia& e) {
    char texto[coruja::kTamLinhaVigia] = {};
    const std::size_t n = coruja::formata_vigia(e, texto, sizeof texto);
    EXPECT_EQ(n, std::strlen(texto));
    return texto;
}

TEST(LinhaVigia, relata_os_sete_campos_numa_linha_so) {
    EXPECT_EQ(formata(completo()),
              "base 18367 pts | fix sim | zona 1 | hdop 0.7 (12 sat) | "
              "temp 26.6 | serial perdeu 0 B | pior volta 268 ms");
}

TEST(LinhaVigia, sem_fix_diz_NAO_em_maiuscula) {
    auto e = completo();
    e.tem_fix = false;
    EXPECT_NE(formata(e).find("fix NAO"), std::string::npos);
}

// ⚠️ Zero e silêncio não podem sair iguais: ver EstadoVigia.
TEST(LinhaVigia, hdop_nao_medido_nao_sai_como_zero) {
    auto e = completo();
    e.tem_qualidade = false;
    e.hdop = 0.0F;
    e.satelites = 0;
    const std::string linha = formata(e);
    EXPECT_NE(linha.find("hdop sem medida"), std::string::npos);
    EXPECT_EQ(linha.find("hdop 0.0"), std::string::npos);
}

TEST(LinhaVigia, temperatura_nao_medida_nao_sai_como_zero) {
    auto e = completo();
    e.tem_temperatura = false;
    e.temperatura_c = 0.0F;
    const std::string linha = formata(e);
    EXPECT_NE(linha.find("temp sem medida"), std::string::npos);
    EXPECT_EQ(linha.find("temp 0.0"), std::string::npos);
}

// A temperatura perdeu a linha própria em 27/10/2026 (uma a cada cinco
// minutos, só com o número). Se ela sumisse da vigia junto, a série térmica
// que motivou a medida teria acabado sem ninguém notar.
TEST(LinhaVigia, a_temperatura_viaja_na_vigia_e_nao_em_linha_propria) {
    EXPECT_NE(formata(completo()).find("temp 26.6"), std::string::npos);
}

TEST(LinhaVigia, serial_perdida_aparece_em_bytes) {
    auto e = completo();
    e.serial_perdidos = 1536;
    EXPECT_NE(formata(e).find("serial perdeu 1536 B"), std::string::npos);
}

TEST(LinhaVigia, nao_escreve_linha_pela_metade_quando_nao_cabe) {
    char texto[32];
    std::memset(texto, 'x', sizeof texto);
    EXPECT_EQ(coruja::formata_vigia(completo(), texto, sizeof texto), 0U);
    EXPECT_STREQ(texto, "");
}

TEST(LinhaVigia, capacidade_zero_nao_escreve_nada) {
    char texto[1] = {'x'};
    EXPECT_EQ(coruja::formata_vigia(completo(), texto, 0), 0U);
}

}  // namespace
