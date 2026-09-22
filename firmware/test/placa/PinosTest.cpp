#include "placa/Pinos.h"

#include <gtest/gtest.h>

#include <set>

namespace {

using namespace coruja::pinos;

// As colisões e a validade já são garantidas por `static_assert` no próprio
// cabeçalho — o compilador recusa antes de existir binário. O que estes testes
// acrescentam é outra coisa: eles **fixam os valores** contra o que está
// soldado e documentado.
//
// Sem isso, trocar o GPIO 2 pelo 3 continuaria compilando e continuaria
// passando em todas as verificações estruturais, e a placa montada pararia de
// funcionar sem ninguém saber por quê. Um teste que falha dizendo "o encoder
// CLK mudou de pino" é a mensagem que se quer receber.

// O GPIO 14 saiu do projeto com o card detect (ADR 0010). Este teste existe
// para que reocupá-lo seja uma decisão consciente: ele quebra se alguém o
// acrescentar sem atualizar a contagem.
TEST(Pinos, GpioQuatorzeEstaLivre) {
    const std::set<unsigned> usados(kTodosOsGpio, kTodosOsGpio + kQuantosGpio);
    EXPECT_EQ(usados.count(14), 0u) << "GPIO 14 voltou a ser usado";
}

TEST(Pinos, ValoresBatemComOBomSchematic) {
    EXPECT_EQ(kGpsTx, 0u);
    EXPECT_EQ(kGpsRx, 1u);
    EXPECT_EQ(kEncoderClk, 2u);
    EXPECT_EQ(kEncoderDt, 3u);
    EXPECT_EQ(kEncoderSw, 4u);
    EXPECT_EQ(kBuzzerBase, 5u);
    // Vermelho e azul estão invertidos em relação à ordem crescente de GPIO,
    // e é intencional: descreve a placa como construída. Ver R-35.
    EXPECT_EQ(kLedVermelho, 8u);
    EXPECT_EQ(kLedVerde, 7u);
    EXPECT_EQ(kLedAzul, 6u);
    EXPECT_EQ(kDisplayBacklight, 15u);
    EXPECT_EQ(kSpiMiso, 16u);
    EXPECT_EQ(kSdCs, 17u);
    EXPECT_EQ(kSpiSck, 18u);
    EXPECT_EQ(kSpiMosi, 19u);
    EXPECT_EQ(kDisplayCs, 20u);
    EXPECT_EQ(kDisplayDc, 21u);
    EXPECT_EQ(kDisplayRst, 22u);
}

TEST(Pinos, AListaCobreTodosOsPinosDeclarados) {
    // Se alguém acrescentar uma constante e esquecer de incluí-la em
    // `kTodosOsGpio`, as verificações de colisão deixam de enxergá-la — e o
    // erro volta a ser silencioso.
    const std::set<unsigned> na_lista(kTodosOsGpio, kTodosOsGpio + kQuantosGpio);
    for (unsigned g : {kGpsTx, kGpsRx, kEncoderClk, kEncoderDt, kEncoderSw,
                       kBuzzerBase, kLedVermelho, kLedVerde, kLedAzul,
                       kSdCs, kSpiMiso, kSpiSck, kSpiMosi, kDisplayCs,
                       kDisplayDc, kDisplayRst, kDisplayBacklight}) {
        EXPECT_EQ(na_lista.count(g), 1u) << "GPIO " << g << " fora de kTodosOsGpio";
    }
    EXPECT_EQ(na_lista.size(), kQuantosGpio) << "há repetição em kTodosOsGpio";
}

TEST(Pinos, NenhumGpioColideComOModuloWiFi) {
    // 23, 24, 25 e 29 são do CYW43 na variante W, e o projeto usa Wi-Fi para a
    // atualização OTA. Também já é `static_assert`, mas a mensagem de teste
    // nomeia o culpado.
    for (std::size_t i = 0; i < kQuantosGpio; ++i) {
        const unsigned g = kTodosOsGpio[i];
        EXPECT_FALSE(g == 23 || g == 24 || g == 25 || g == 29)
            << "GPIO " << g << " pertence ao CYW43";
    }
}

TEST(Pinos, DisplayESdCompartilhamOSpiMasTemCsSeparados) {
    // O compartilhamento é intencional (RNF06) e exige mutex. O que não pode é
    // o CS ser o mesmo, o que selecionaria os dois ao mesmo tempo.
    EXPECT_NE(kSdCs, kDisplayCs);
}

TEST(Pinos, PinosDeAlimentacaoNaoEstaoNaListaDeGpio) {
    // `kPinoFisico*` são números de pino físico, não GPIO. Passar um deles a
    // gpio_init() configuraria um GPIO alheio.
    const std::set<unsigned> gpio(kTodosOsGpio, kTodosOsGpio + kQuantosGpio);
    EXPECT_EQ(gpio.count(kPinoFisico3V3Out), 0u);
    EXPECT_EQ(gpio.count(kPinoFisico3V3En), 0u);
    EXPECT_EQ(gpio.count(kPinoFisicoVsys), 0u);
    EXPECT_EQ(gpio.count(kPinoFisicoGnd), 0u);
}

TEST(Pinos, TresVigiaAdjacenteAoEnable) {
    // A razão de `kPinoFisico3V3En` existir: ele é o pino seguinte ao
    // `3V3_OUT`, e quatro coisas do projeto vão no 36. Se esta relação deixar
    // de valer, o aviso do bom_schematic perde sentido.
    EXPECT_EQ(kPinoFisico3V3En, kPinoFisico3V3Out + 1);
}

}  // namespace
