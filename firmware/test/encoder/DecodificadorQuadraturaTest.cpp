#include "encoder/DecodificadorQuadratura.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

using namespace coruja;

/// Um detente horário: 11 -> 01 -> 00 -> 10 -> 11. Como estado e (clk, dt),
/// isso e (1,1) (0,1) (0,0) (1,0) (1,1).
const std::vector<std::pair<bool, bool>> kHorario = {
    {true, true}, {false, true}, {false, false}, {true, false}, {true, true}};

const std::vector<std::pair<bool, bool>> kAntiHorario = {
    {true, true}, {true, false}, {false, false}, {false, true}, {true, true}};

int roda(DecodificadorQuadratura& d,
         const std::vector<std::pair<bool, bool>>& seq, int voltas = 1) {
    int passos = 0;
    for (int v = 0; v < voltas; ++v) {
        for (const auto& [clk, dt] : seq) {
            passos += d.amostra(clk, dt);
        }
    }
    return passos;
}

TEST(Decodificador, UmDetenteHorarioDaUmPassoPositivo) {
    DecodificadorQuadratura d;
    EXPECT_EQ(roda(d, kHorario), +1);
}

TEST(Decodificador, UmDetenteAntiHorarioDaUmPassoNegativo) {
    DecodificadorQuadratura d;
    EXPECT_EQ(roda(d, kAntiHorario), -1);
}

TEST(Decodificador, DezDetentesDaoDezPassos) {
    DecodificadorQuadratura d;
    EXPECT_EQ(roda(d, kHorario, 10), +10);
}

TEST(Decodificador, InvertidoTrocaOSinal) {
    DecodificadorQuadratura d(true);
    EXPECT_EQ(roda(d, kHorario), -1);
    DecodificadorQuadratura e(true);
    EXPECT_EQ(roda(e, kAntiHorario), +1);
}

TEST(Decodificador, APrimeiraAmostraNaoGeraPasso) {
    // Sem isto, o boot ou um reinicia() produziria um passo fantasma.
    DecodificadorQuadratura d;
    EXPECT_EQ(d.amostra(false, false), 0);
    EXPECT_EQ(d.acumulado(), 0);
}

TEST(Decodificador, AmostrasRepetidasNaoAcumulam) {
    DecodificadorQuadratura d;
    d.amostra(true, true);
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(d.amostra(true, true), 0);
    }
    EXPECT_EQ(d.acumulado(), 0);
}

TEST(Decodificador, MeioDetenteNaoEmitePasso) {
    // Girar até a metade e voltar não é um clique de detente, e não deve
    // mexer no brilho.
    DecodificadorQuadratura d;
    d.amostra(true, true);
    EXPECT_EQ(d.amostra(false, true), 0);
    EXPECT_EQ(d.amostra(false, false), 0);
    EXPECT_EQ(d.acumulado(), 2);
    // volta
    EXPECT_EQ(d.amostra(false, true), 0);
    EXPECT_EQ(d.amostra(true, true), 0);
    EXPECT_EQ(d.acumulado(), 0);
}

TEST(Decodificador, TransicaoImpossivelContribuiZero) {
    // Os dois canais mudando na mesma amostra é fisicamente impossível num
    // encoder em quadratura: é ruído. Contar como meio passo é o bug clássico
    // que faz o brilho saltar, e é por isso que a tabela tem zeros.
    DecodificadorQuadratura d;
    d.amostra(true, true);           // referência 11
    EXPECT_EQ(d.amostra(false, false), 0);  // 11 -> 00, impossível
    EXPECT_EQ(d.acumulado(), 0);
}

TEST(Decodificador, RuidoNaoProduzPassoLiquido) {
    // Alterna entre dois estados adjacentes muitas vezes, como um contato
    // sujo parado em cima de uma borda. Não deve emitir nenhum detente.
    DecodificadorQuadratura d;
    d.amostra(true, true);
    int passos = 0;
    for (int i = 0; i < 200; ++i) {
        passos += d.amostra(i % 2 == 0, true);
    }
    EXPECT_EQ(passos, 0) << "ruído de borda virou giro";
}

TEST(Decodificador, GiroHorarioDepoisDeAntiHorarioNaoSomaErrado) {
    DecodificadorQuadratura d;
    EXPECT_EQ(roda(d, kHorario, 3), +3);
    EXPECT_EQ(roda(d, kAntiHorario, 3), -3);
}

TEST(Decodificador, ReiniciaZeraOAcumuladoSemEmitir) {
    DecodificadorQuadratura d;
    d.amostra(true, true);
    d.amostra(false, true);
    ASSERT_NE(d.acumulado(), 0);
    d.reinicia();
    EXPECT_EQ(d.acumulado(), 0);
    EXPECT_EQ(d.amostra(false, false), 0);  // primeira depois do reinicia
}

TEST(Decodificador, SequenciaCompletaPercorreOsQuatroEstados) {
    // Confirma que um detente são exatamente 4 transições válidas, e que o
    // passo sai na quarta, não antes.
    DecodificadorQuadratura d;
    d.amostra(true, true);
    EXPECT_EQ(d.amostra(false, true), 0);
    EXPECT_EQ(d.amostra(false, false), 0);
    EXPECT_EQ(d.amostra(true, false), 0);
    EXPECT_EQ(d.amostra(true, true), +1);
}

}  // namespace
