#include "app/ModoTesteEncoder.h"

#include <gtest/gtest.h>

#include "apoio/EncoderMock.h"
#include "apoio/LedRgbMock.h"

namespace {

using namespace coruja;

TEST(ModoTeste, ComecaApagado) {
    ModoTesteEncoder m;
    EXPECT_EQ(m.cor(), cores::kApagado);
}

TEST(ModoTeste, GiroEsquerdaAcendeVermelho) {
    ModoTesteEncoder m;
    EXPECT_EQ(m.aplica(EventoEncoder::GiroEsquerda), cores::kVermelho);
}

TEST(ModoTeste, GiroDireitaAcendeAzul) {
    ModoTesteEncoder m;
    EXPECT_EQ(m.aplica(EventoEncoder::GiroDireita), cores::kAzul);
}

TEST(ModoTeste, CliqueApaga) {
    ModoTesteEncoder m;
    m.aplica(EventoEncoder::GiroEsquerda);
    EXPECT_EQ(m.aplica(EventoEncoder::Clique), cores::kApagado);
}

TEST(ModoTeste, EventoNenhumMantemACor) {
    // Quem chama pode invocar a cada ciclo do laço sem piscar o LED.
    ModoTesteEncoder m;
    m.aplica(EventoEncoder::GiroDireita);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(m.aplica(EventoEncoder::Nenhum), cores::kAzul);
    }
}

TEST(ModoTeste, GirosSeguidosNaMesmaDirecaoNaoMudamNada) {
    ModoTesteEncoder m;
    EXPECT_EQ(m.aplica(EventoEncoder::GiroEsquerda), cores::kVermelho);
    EXPECT_EQ(m.aplica(EventoEncoder::GiroEsquerda), cores::kVermelho);
}

TEST(ModoTeste, TrocaDeDirecaoTrocaACor) {
    ModoTesteEncoder m;
    m.aplica(EventoEncoder::GiroEsquerda);
    EXPECT_EQ(m.aplica(EventoEncoder::GiroDireita), cores::kAzul);
    EXPECT_EQ(m.aplica(EventoEncoder::GiroEsquerda), cores::kVermelho);
}

TEST(ModoTeste, CliqueComLedJaApagadoContinuaApagado) {
    ModoTesteEncoder m;
    EXPECT_EQ(m.aplica(EventoEncoder::Clique), cores::kApagado);
    EXPECT_EQ(m.aplica(EventoEncoder::Clique), cores::kApagado);
}

// --- a cadeia inteira contra os mocks (regra 2) ---

TEST(CadeiaEncoderLed, PercorreUmaSessaoDeBancada) {
    teste::EncoderMock enc{
        EventoEncoder::GiroEsquerda,
        EventoEncoder::Nenhum,
        EventoEncoder::GiroDireita,
        EventoEncoder::Clique,
        EventoEncoder::GiroEsquerda,
    };
    teste::LedRgbMock led;
    ModoTesteEncoder modo;

    while (!enc.vazio()) {
        led.define_cor(modo.aplica(enc.proximo_evento()));
    }

    const std::vector<Cor> esperado = {
        cores::kVermelho, cores::kVermelho,  // Nenhum mantém
        cores::kAzul, cores::kApagado, cores::kVermelho,
    };
    EXPECT_EQ(led.historico(), esperado);
    EXPECT_EQ(led.cor_atual(), cores::kVermelho);
}

TEST(CadeiaEncoderLed, EncoderVazioDevolveNenhumIndefinidamente) {
    teste::EncoderMock enc;
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(enc.proximo_evento(), EventoEncoder::Nenhum);
    }
}

TEST(CadeiaEncoderLed, ApagaDaInterfaceZeraOsTresCanais) {
    teste::LedRgbMock led;
    led.define_cor(Cor{10, 20, 30});
    led.apaga();
    EXPECT_EQ(led.cor_atual(), cores::kApagado);
}

TEST(Cor, ComparacaoEPorCanal) {
    EXPECT_EQ((Cor{1, 2, 3}), (Cor{1, 2, 3}));
    EXPECT_NE((Cor{1, 2, 3}), (Cor{1, 2, 4}));
    EXPECT_NE(cores::kVermelho, cores::kAzul);
}

TEST(EventoEncoder, TodosOsEventosTemNome) {
    for (auto e : {EventoEncoder::Nenhum, EventoEncoder::GiroEsquerda,
                   EventoEncoder::GiroDireita, EventoEncoder::Clique}) {
        EXPECT_STRNE(nome_evento(e), "?");
    }
}

}  // namespace
