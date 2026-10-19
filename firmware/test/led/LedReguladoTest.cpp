// O interruptor do LED RGB.
//
// 🔴 **Relato de 19/10/2026:** a noite o LED de 10 mm difuso ilumina a cabine
// e incomoda mais do que informa. O desligamento e do usuario, no menu.

#include "led/LedRegulado.h"

#include <gtest/gtest.h>

#include "apoio/LedRgbMock.h"
#include "led/Calibracao.h"

namespace {

using namespace coruja;
using coruja::teste::LedRgbMock;

TEST(LedRegulado, ligado_repassa_a_cor) {
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_cor(cores::kVermelho);
    EXPECT_EQ(alvo.cor_atual(), cores::kVermelho);
}

TEST(LedRegulado, desligado_nao_acende_nada) {
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.liga(false);
    led.define_cor(cores::kVermelho);
    EXPECT_EQ(alvo.cor_atual(), cores::kApagado);
}

TEST(LedRegulado, desligar_apaga_o_que_ja_estava_aceso) {
    // Esperar o proximo alerta para apagar deixaria o LED aceso por
    // quilometros depois do clique -- o clique pareceria sem efeito.
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_cor(cores::kVermelho);
    led.liga(false);
    EXPECT_EQ(alvo.cor_atual(), cores::kApagado);
}

TEST(LedRegulado, religar_reacende_na_cor_desejada) {
    // ⚠️ Sem lembrar a cor, religar deixaria o LED apagado ate o proximo
    // alerta -- e num trecho sem radar isso e quilometros.
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_cor(cores::kVerde);
    led.liga(false);
    ASSERT_EQ(alvo.cor_atual(), cores::kApagado);
    led.liga(true);
    EXPECT_EQ(alvo.cor_atual(), cores::kVerde);
}

TEST(LedRegulado, a_cor_desejada_continua_sendo_registrada_com_ele_apagado) {
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.liga(false);
    led.define_cor(cores::kVerde);
    EXPECT_EQ(led.cor_desejada(), cores::kVerde);
    EXPECT_EQ(alvo.cor_atual(), cores::kApagado);
}

TEST(LedRegulado, cor_atual_diz_o_que_esta_ACESO) {
    // Quem consulta desenha o LED na previa do host. Devolver a cor desejada
    // faria o LED desligado mentir na tela.
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.liga(false);
    led.define_cor(cores::kVermelho);
    EXPECT_EQ(led.cor_atual(), cores::kApagado);
}

TEST(LedRegulado, ligar_de_novo_nao_toca_no_hardware) {
    // A aplicacao chama a cada volta do laco. Reescrever tres canais de PWM
    // centenas de vezes por segundo seria trabalho por nada.
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_cor(cores::kVermelho);
    const auto escritas = alvo.escritas();
    led.liga(true);
    led.liga(true);
    EXPECT_EQ(alvo.escritas(), escritas);
}

TEST(LedRegulado, nasce_ligado) {
    // Quem nunca mexeu no menu tem o aparelho de sempre.
    LedRgbMock alvo;
    LedRegulado led{alvo};
    EXPECT_TRUE(led.ligado());
}

TEST(LedRegulado, apaga_funciona_com_ele_ligado) {
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_cor(cores::kVermelho);
    led.apaga();
    EXPECT_EQ(alvo.cor_atual(), cores::kApagado);
}


// ============================================== a intensidade (dim do LED)
//
// 🔴 Pedido de 19/10/2026, depois do interruptor: o LED incomoda **a noite**,
// e desligar de vez tira tambem o de dia. Presets por periodo, como os da
// tela.

TEST(LedRegulado, cem_por_cento_nao_mexe_na_cor) {
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_presets(100, 20);
    led.define_cor(calibracao::kAmbar);
    EXPECT_EQ(alvo.cor_atual(), calibracao::kAmbar);
}

TEST(LedRegulado, a_intensidade_escala_os_tres_canais) {
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_presets(50, 20);
    led.define_cor(Cor{200, 100, 50});
    EXPECT_EQ(alvo.cor_atual(), (Cor{100, 50, 25}));
}

TEST(LedRegulado, o_matiz_do_ambar_sobrevive_ate_o_piso) {
    // ⚠️ **O teste que justifica a escala LINEAR.** O ambar e uma RAZAO
    // entre canais de 8 bits -- 50/255 = 0,196 --, e uma curva perceptual
    // levaria o canal secundario a um digito, onde a quantizacao destroi a
    // razao e o ambar vira vermelho alaranjado. A calibracao do R-05 existe
    // para esses matizes nao mentirem.
    LedRgbMock alvo;
    LedRegulado led{alvo};
    const double alvo_razao = 50.0 / 255.0;
    for (std::uint8_t pct : {100, 50, 25, 10, 5}) {
        led.define_presets(pct, pct);
        led.define_cor(calibracao::kAmbar);
        const Cor c = alvo.cor_atual();
        ASSERT_GT(c.r, 0) << "pct=" << static_cast<int>(pct);
        ASSERT_GT(c.g, 0) << "o canal secundario apagou em pct="
                          << static_cast<int>(pct);
        const double razao = static_cast<double>(c.g) / c.r;
        EXPECT_NEAR(razao, alvo_razao, 0.06)
            << "o matiz derivou em pct=" << static_cast<int>(pct);
    }
}

TEST(LedRegulado, canal_apagado_continua_apagado) {
    // O azul do ambar e zero. Acender 1 nele seria inventar cor.
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_presets(5, 5);
    led.define_cor(calibracao::kAmbar);
    EXPECT_EQ(alvo.cor_atual().b, 0);
}

TEST(LedRegulado, no_piso_nenhuma_cor_da_paleta_perde_canal) {
    // ⚠️ **Este teste esta no lugar de um piso por canal.** A versao
    // anterior forcava a 1 todo canal aceso, e a mutacao mostrou que era
    // codigo morto: o menor canal secundario da paleta e 40, e a 5% o
    // arredondamento ja o leva a 2.
    //
    // Uma cor nova com canal muito pequeno reprova aqui, e quem a
    // acrescentar decide o que fazer -- em vez de ganhar de graca um piso
    // que mente sobre a razao entre os canais.
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_presets(kIntensidadeLedMinPct, kIntensidadeLedMinPct);
    for (const Cor& c : {calibracao::kSegura, calibracao::kAmbar,
                         calibracao::kRosa, calibracao::kPerigo}) {
        led.define_cor(c);
        const Cor saida = alvo.cor_atual();
        EXPECT_EQ(saida.r == 0, c.r == 0) << "canal R sumiu ou nasceu";
        EXPECT_EQ(saida.g == 0, c.g == 0) << "canal G sumiu ou nasceu";
        EXPECT_EQ(saida.b == 0, c.b == 0) << "canal B sumiu ou nasceu";
    }
}

TEST(LedRegulado, a_noite_usa_o_preset_da_noite) {
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_presets(100, 20);
    led.define_cor(cores::kVermelho);
    ASSERT_EQ(alvo.cor_atual(), cores::kVermelho);
    led.define_periodo(PeriodoDoDia::Noite);
    EXPECT_EQ(led.percentual(), 20);
    EXPECT_EQ(alvo.cor_atual(), (Cor{51, 0, 0}));
}

TEST(LedRegulado, a_virada_de_periodo_vale_SEM_esperar_o_proximo_alerta) {
    // Num trecho sem radar o proximo alerta pode demorar quilometros, e o
    // LED ficaria forte a noite inteira ate la.
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_presets(100, 10);
    led.define_cor(cores::kVerde);
    const auto escritas = alvo.escritas();
    led.define_periodo(PeriodoDoDia::Noite);
    EXPECT_GT(alvo.escritas(), escritas);
    EXPECT_EQ(alvo.cor_atual(), (Cor{0, 26, 0}));
}

TEST(LedRegulado, periodo_desconhecido_nao_muda_nada) {
    // ⚠️ Mesma regra do `Brilho`, e tem de ser: sem ela, perder o fix
    // clarearia o LED enquanto a tela continuasse escura.
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_presets(100, 10);
    led.define_periodo(PeriodoDoDia::Noite);
    led.define_cor(cores::kVerde);
    const Cor de_noite = alvo.cor_atual();
    led.define_periodo(PeriodoDoDia::Desconhecido);
    EXPECT_EQ(alvo.cor_atual(), de_noite);
    EXPECT_EQ(led.percentual(), 10);
}

TEST(LedRegulado, o_mesmo_periodo_de_novo_nao_toca_no_hardware) {
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_cor(cores::kVermelho);
    led.define_periodo(PeriodoDoDia::Dia);
    const auto escritas = alvo.escritas();
    led.define_periodo(PeriodoDoDia::Dia);
    EXPECT_EQ(alvo.escritas(), escritas);
}

TEST(LedRegulado, o_piso_e_cinco_porque_zero_e_o_interruptor) {
    // Dois controles que apagam o LED disputariam o mesmo resultado, e o
    // menu ficaria com um ajuste que faz o que o outro ja faz.
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_presets(0, 0);
    EXPECT_EQ(led.percentual(), kIntensidadeLedMinPct);
    led.define_cor(cores::kVermelho);
    EXPECT_GT(alvo.cor_atual().r, 0);
}

TEST(LedRegulado, acima_de_cem_vai_para_cem) {
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_presets(200, 200);
    EXPECT_EQ(led.percentual(), kIntensidadeLedMaxPct);
}

TEST(LedRegulado, desligado_a_intensidade_nao_acende_nada) {
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.liga(false);
    led.define_presets(100, 100);
    led.define_cor(cores::kVermelho);
    EXPECT_EQ(alvo.cor_atual(), cores::kApagado);
}

TEST(LedRegulado, religar_reacende_ja_atenuado) {
    LedRgbMock alvo;
    LedRegulado led{alvo};
    led.define_presets(50, 50);
    led.define_cor(cores::kVermelho);
    led.liga(false);
    led.liga(true);
    EXPECT_EQ(alvo.cor_atual(), (Cor{128, 0, 0}));
}

}  // namespace
