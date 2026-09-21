#include "app/CicloCores.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

TEST(CicloCores, ComecaEmZonaSegura) {
    CicloCores c;
    EXPECT_EQ(c.estado(), EstadoVia::Segura);
    EXPECT_EQ(c.cor(), calibracao::kSegura);
}

TEST(CicloCores, DireitaAgravaAteOVermelho) {
    CicloCores c;
    EXPECT_EQ(c.aplica(EventoEncoder::GiroDireita), EstadoVia::Ambar);
    EXPECT_EQ(c.aplica(EventoEncoder::GiroDireita), EstadoVia::Rosa);
    EXPECT_EQ(c.aplica(EventoEncoder::GiroDireita), EstadoVia::Perigo);
    EXPECT_EQ(c.cor(), calibracao::kPerigo);
}

TEST(CicloCores, EsquerdaAliviaDeVolta) {
    CicloCores c;
    c.aplica(EventoEncoder::GiroDireita);
    c.aplica(EventoEncoder::GiroDireita);
    EXPECT_EQ(c.aplica(EventoEncoder::GiroEsquerda), EstadoVia::Ambar);
    EXPECT_EQ(c.aplica(EventoEncoder::GiroEsquerda), EstadoVia::Segura);
}

TEST(CicloCores, SaturaNasPontasEAvisa) {
    // Não circula de propósito: numa tela de diagnóstico, voltar sozinho ao
    // verde depois do vermelho esconde justamente que se chegou ao fim.
    CicloCores c;
    EXPECT_EQ(c.aplica(EventoEncoder::GiroEsquerda), EstadoVia::Segura);
    EXPECT_TRUE(c.na_ponta());

    for (int i = 0; i < 3; ++i) {
        c.aplica(EventoEncoder::GiroDireita);
        EXPECT_FALSE(c.na_ponta());
    }
    EXPECT_EQ(c.aplica(EventoEncoder::GiroDireita), EstadoVia::Perigo);
    EXPECT_TRUE(c.na_ponta());
}

TEST(CicloCores, CliqueNaoMexeNaCor) {
    // O clique é da atualização OTA. Se ele andasse na cor, uma atualização
    // mudaria o estado mostrado no LED sem que ninguém tivesse girado.
    CicloCores c;
    c.aplica(EventoEncoder::GiroDireita);
    EXPECT_EQ(c.aplica(EventoEncoder::Clique), EstadoVia::Ambar);
    EXPECT_EQ(c.aplica(EventoEncoder::Nenhum), EstadoVia::Ambar);
}

TEST(CicloCores, AsQuatroCoresSaoDistintasEVemDaCalibracaoMedida) {
    // Se duas colidirem, o teste de bancada não consegue distinguir os
    // estados — e foi exatamente o sintoma do LED com pernas trocadas (R-35).
    const Cor cores[] = {cor_do_estado(EstadoVia::Segura),
                         cor_do_estado(EstadoVia::Ambar),
                         cor_do_estado(EstadoVia::Rosa),
                         cor_do_estado(EstadoVia::Perigo)};
    for (std::size_t i = 0; i < kQuantosEstados; ++i) {
        for (std::size_t j = i + 1; j < kQuantosEstados; ++j) {
            EXPECT_NE(cores[i], cores[j]) << i << " e " << j;
        }
    }
    EXPECT_EQ(cores[0], calibracao::kSegura);
    EXPECT_EQ(cores[3], calibracao::kPerigo);
}

TEST(CicloCores, TodoEstadoTemNome) {
    for (std::size_t i = 0; i < kQuantosEstados; ++i) {
        const char* n = nome_estado(static_cast<EstadoVia>(i));
        ASSERT_NE(n, nullptr);
        EXPECT_STRNE(n, "?") << "estado " << i;
    }
}

}  // namespace
