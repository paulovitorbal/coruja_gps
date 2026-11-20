#include "nucleo/EsperaVigiada.h"

#include <gtest/gtest.h>

#include <stdexcept>

#include "apoio/CaoDeGuardaFalso.h"

namespace {

using namespace coruja;

class RelogioFalso : public Pausa {
public:
    std::uint32_t agora = 1'000'000;
    void espera_ms(std::uint32_t ms) override { agora += ms; }
    std::uint32_t agora_ms() override { return agora; }
};

/// Uma fase de rede (DNS, handshake, resposta): `duracao_ms` de relogio, um
/// passo de 1 ms de cada vez, como o `poll` + `sleep_ms(1)` do aparelho.
FimDaEspera fase(RelogioFalso& r, CaoDeGuarda* cao, std::uint32_t duracao_ms) {
    const std::uint32_t fim = r.agora + duracao_ms;
    return espera_vigiada(
        cao, [&] { return r.agora >= fim; }, [] { return false; },
        [&] { r.espera_ms(1); });
}

TEST(EsperaVigiada, alimenta_a_cada_volta_depois_do_passo) {
    teste::CaoDeGuardaFalso cao;
    int voltas = 0;
    unsigned vistas_pelo_passo = 99;
    const auto fim = espera_vigiada(
        &cao, [&] { return voltas >= 5; }, [] { return false; },
        [&] {
            // A refeicao da volta N+1 ainda nao aconteceu: vem DEPOIS.
            vistas_pelo_passo = cao.alimentacoes;
            ++voltas;
        });
    EXPECT_EQ(fim, FimDaEspera::Pronto);
    EXPECT_EQ(cao.alimentacoes, 5U);
    EXPECT_EQ(vistas_pelo_passo, 4U)
        << "alimentou ANTES do passo: um poll travado nao seria pego";
}

TEST(EsperaVigiada, um_passo_que_trava_faz_a_refeicao_parar) {
    // O poll "trava" (nunca volta): modelado por uma excecao, que e a
    // unica forma de um passo nao voltar sem pendurar o teste.
    teste::CaoDeGuardaFalso cao;
    int voltas = 0;
    EXPECT_THROW(
        espera_vigiada(
            &cao, [] { return false; }, [] { return false; },
            [&] {
                if (++voltas == 4) { throw std::runtime_error("travou"); }
            }),
        std::runtime_error);
    EXPECT_EQ(cao.alimentacoes, 3U)
        << "o cao comeu por uma volta que nunca fechou";
}

TEST(EsperaVigiada, alimenta_antes_de_devolver_o_tempo_esgotado) {
    // O chamador vai logar a desistencia, e o log forca descarga no cartao.
    teste::CaoDeGuardaFalso cao;
    int voltas = 0;
    const auto fim = espera_vigiada(
        &cao, [] { return false; }, [&] { return voltas >= 3; },
        [&] { ++voltas; });
    EXPECT_EQ(fim, FimDaEspera::Expirou);
    EXPECT_EQ(cao.alimentacoes, 4U) << "3 voltas e a refeicao da desistencia";
}

TEST(EsperaVigiada, expirar_na_primeira_checagem_tambem_alimenta) {
    teste::CaoDeGuardaFalso cao;
    const auto fim = espera_vigiada(
        &cao, [] { return false; }, [] { return true; }, [] {});
    EXPECT_EQ(fim, FimDaEspera::Expirou);
    EXPECT_EQ(cao.alimentacoes, 1U);
}

TEST(EsperaVigiada, ja_pronto_nao_roda_o_passo_nem_alimenta) {
    teste::CaoDeGuardaFalso cao;
    bool rodou = false;
    const auto fim = espera_vigiada(
        &cao, [] { return true; }, [] { return false; },
        [&] { rodou = true; });
    EXPECT_EQ(fim, FimDaEspera::Pronto);
    EXPECT_FALSE(rodou);
    EXPECT_EQ(cao.alimentacoes, 0U);
}

TEST(EsperaVigiada, sem_cao_a_espera_roda_igual) {
    int voltas = 0;
    EXPECT_EQ(espera_vigiada(
                  nullptr, [&] { return voltas >= 2; }, [] { return false; },
                  [&] { ++voltas; }),
              FimDaEspera::Pronto);
    EXPECT_EQ(voltas, 2);
}

// ---------------------------------------------------------------------------
// O defeito do HIGH 1, em fases SEQUENCIAIS. Um wi-fi a -88 dBm: DNS 2 s,
// handshake 9 s, resposta 6 s. Cada fase cabe nos 15 s de inatividade; a
// soma, 17 s, passa dos 16,5 s do cao -- e nenhuma fase "travou".

TEST(EsperaVigiada, tres_fases_vivas_que_somam_mais_que_o_prazo_nao_mordem) {
    RelogioFalso r;
    teste::CaoCronometrado cao(r);
    fase(r, &cao, 2'000);
    fase(r, &cao, 9'000);
    fase(r, &cao, 6'000);
    cao.fecha();
    EXPECT_GT(r.agora - 1'000'000u, kPrazoCaoMs)
        << "o cenario nem passa do prazo: nao prova nada";
    EXPECT_LE(cao.maior_intervalo_ms, 10u)
        << "houve silencio de " << cao.maior_intervalo_ms << " ms";
}

TEST(EsperaVigiada, o_mesmo_cenario_sem_refeicao_e_detectado_como_mordida) {
    // O contraponto: sem alimentar por dentro das fases (como era), o
    // cronometro enxerga os 17 s e a conta estoura. E o teste que teria
    // falhado com o defeito.
    RelogioFalso r;
    teste::CaoCronometrado cao(r);
    fase(r, nullptr, 2'000);
    fase(r, nullptr, 9'000);
    fase(r, nullptr, 6'000);
    cao.alimenta();   // so a refeicao da ponta do pedido
    EXPECT_GT(cao.maior_intervalo_ms, kPrazoCaoMs);
}

}  // namespace
