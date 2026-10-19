// O interruptor do LED RGB.
//
// 🔴 **Relato de 19/10/2026:** a noite o LED de 10 mm difuso ilumina a cabine
// e incomoda mais do que informa. O desligamento e do usuario, no menu.

#include "led/LedChaveado.h"

#include <gtest/gtest.h>

#include "apoio/LedRgbMock.h"

namespace {

using namespace coruja;
using coruja::teste::LedRgbMock;

TEST(LedChaveado, ligado_repassa_a_cor) {
    LedRgbMock alvo;
    LedChaveado led{alvo};
    led.define_cor(cores::kVermelho);
    EXPECT_EQ(alvo.cor_atual(), cores::kVermelho);
}

TEST(LedChaveado, desligado_nao_acende_nada) {
    LedRgbMock alvo;
    LedChaveado led{alvo};
    led.liga(false);
    led.define_cor(cores::kVermelho);
    EXPECT_EQ(alvo.cor_atual(), cores::kApagado);
}

TEST(LedChaveado, desligar_apaga_o_que_ja_estava_aceso) {
    // Esperar o proximo alerta para apagar deixaria o LED aceso por
    // quilometros depois do clique -- o clique pareceria sem efeito.
    LedRgbMock alvo;
    LedChaveado led{alvo};
    led.define_cor(cores::kVermelho);
    led.liga(false);
    EXPECT_EQ(alvo.cor_atual(), cores::kApagado);
}

TEST(LedChaveado, religar_reacende_na_cor_desejada) {
    // ⚠️ Sem lembrar a cor, religar deixaria o LED apagado ate o proximo
    // alerta -- e num trecho sem radar isso e quilometros.
    LedRgbMock alvo;
    LedChaveado led{alvo};
    led.define_cor(cores::kVerde);
    led.liga(false);
    ASSERT_EQ(alvo.cor_atual(), cores::kApagado);
    led.liga(true);
    EXPECT_EQ(alvo.cor_atual(), cores::kVerde);
}

TEST(LedChaveado, a_cor_desejada_continua_sendo_registrada_com_ele_apagado) {
    LedRgbMock alvo;
    LedChaveado led{alvo};
    led.liga(false);
    led.define_cor(cores::kVerde);
    EXPECT_EQ(led.cor_desejada(), cores::kVerde);
    EXPECT_EQ(alvo.cor_atual(), cores::kApagado);
}

TEST(LedChaveado, cor_atual_diz_o_que_esta_ACESO) {
    // Quem consulta desenha o LED na previa do host. Devolver a cor desejada
    // faria o LED desligado mentir na tela.
    LedRgbMock alvo;
    LedChaveado led{alvo};
    led.liga(false);
    led.define_cor(cores::kVermelho);
    EXPECT_EQ(led.cor_atual(), cores::kApagado);
}

TEST(LedChaveado, ligar_de_novo_nao_toca_no_hardware) {
    // A aplicacao chama a cada volta do laco. Reescrever tres canais de PWM
    // centenas de vezes por segundo seria trabalho por nada.
    LedRgbMock alvo;
    LedChaveado led{alvo};
    led.define_cor(cores::kVermelho);
    const auto escritas = alvo.escritas();
    led.liga(true);
    led.liga(true);
    EXPECT_EQ(alvo.escritas(), escritas);
}

TEST(LedChaveado, nasce_ligado) {
    // Quem nunca mexeu no menu tem o aparelho de sempre.
    LedRgbMock alvo;
    LedChaveado led{alvo};
    EXPECT_TRUE(led.ligado());
}

TEST(LedChaveado, apaga_funciona_com_ele_ligado) {
    LedRgbMock alvo;
    LedChaveado led{alvo};
    led.define_cor(cores::kVermelho);
    led.apaga();
    EXPECT_EQ(alvo.cor_atual(), cores::kApagado);
}

}  // namespace
