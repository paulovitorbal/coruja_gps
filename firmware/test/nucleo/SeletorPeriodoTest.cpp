#include "nucleo/SeletorPeriodo.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

/// Sao Paulo, 21/06/2026 (inverno). Nascer ~06:47, por ~17:28 (UTC-3).
Telemetria em(int hora_utc, int minuto) {
    Telemetria t;
    t.data_valida = true;
    t.lat = -23.55F;
    t.lon = -46.63F;
    t.ano = 2026;
    t.mes = 6;
    t.dia = 21;
    t.hora = static_cast<std::uint8_t>(hora_utc);
    t.minuto = static_cast<std::uint8_t>(minuto);
    return t;
}

TEST(SeletorPeriodo, AutomaticoSegueOCeu) {
    SeletorPeriodo s;
    EXPECT_EQ(s.atualiza(em(15, 0), 0), PeriodoDoDia::Dia);    // 12:00 local
    EXPECT_EQ(s.atualiza(em(3, 0), 100000), PeriodoDoDia::Noite);  // 00:00
}

TEST(SeletorPeriodo, SempreDiaIgnoraOCeu) {
    // A garagem coberta ao meio-dia e a rua iluminada de madrugada: se
    // dependessem do ceu, os modos forcados nao serviriam para nada.
    SeletorPeriodo s;
    s.define_modo(ModoNoturno::SempreDia);
    EXPECT_EQ(s.atualiza(em(3, 0), 0), PeriodoDoDia::Dia);
}

TEST(SeletorPeriodo, SempreNoiteIgnoraOCeu) {
    SeletorPeriodo s;
    s.define_modo(ModoNoturno::SempreNoite);
    EXPECT_EQ(s.atualiza(em(15, 0), 0), PeriodoDoDia::Noite);
}

TEST(SeletorPeriodo, VoltarParaAutomaticoNaoEsperaORecalculo) {
    // O calculo segue em dia por baixo do modo forcado; se nao seguisse,
    // voltar a 'auto' deixaria a tela no valor de ate um minuto atras.
    SeletorPeriodo s;
    s.define_modo(ModoNoturno::SempreNoite);
    s.atualiza(em(15, 0), 0);
    s.define_modo(ModoNoturno::Automatico);
    EXPECT_EQ(s.atualiza(em(15, 0), 1000), PeriodoDoDia::Dia);
}

TEST(SeletorPeriodo, NaoRecalculaAntesDoIntervalo) {
    // Prova pelo efeito: a telemetria muda de dia para noite, mas dentro
    // do intervalo o veredito nao pode mudar.
    SeletorPeriodo s;
    EXPECT_EQ(s.atualiza(em(15, 0), 0), PeriodoDoDia::Dia);
    EXPECT_EQ(s.atualiza(em(3, 0), kIntervaloRecalculoMs - 1),
              PeriodoDoDia::Dia);
    EXPECT_EQ(s.atualiza(em(3, 0), kIntervaloRecalculoMs),
              PeriodoDoDia::Noite);
}

TEST(SeletorPeriodo, OPrimeiroCalculoNaoEspera) {
    SeletorPeriodo s;
    EXPECT_EQ(s.atualiza(em(3, 0), 999999), PeriodoDoDia::Noite);
}

TEST(SeletorPeriodo, SemDataOPeriodoEDesconhecido) {
    SeletorPeriodo s;
    Telemetria t;
    t.data_valida = false;
    EXPECT_EQ(s.atualiza(t, 0), PeriodoDoDia::Desconhecido);
}

TEST(SeletorPeriodo, DesconhecidoNaoMexeNoBrilho) {
    // Contrato com o Brilho: 'Desconhecido' nao troca preset nenhum.
    SeletorPeriodo s;
    Telemetria t;
    t.data_valida = false;
    EXPECT_EQ(s.atualiza(t, 0), PeriodoDoDia::Desconhecido);
    EXPECT_EQ(s.periodo(), PeriodoDoDia::Desconhecido);
}

}  // namespace
