#include "placa/Termometro.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

/// A leitura crua que corresponde a uma tensão dada. Calculada aqui, e não
/// copiada de uma tabela: um número escrito à mão concordaria com um
/// conversor errado.
std::uint16_t bruto_para(float tensao) {
    return static_cast<std::uint16_t>(tensao * 4095.0F / 3.3F + 0.5F);
}

TEST(Termometro, a_tensao_de_referencia_da_27_graus) {
    // 0,706 V é o ponto que o datasheet ancora. Se a conversão errar aqui,
    // erra em todo lugar.
    EXPECT_NEAR(celsius_do_adc(bruto_para(0.706F)), 27.0F, 0.5F);
}

TEST(Termometro, a_inclinacao_e_NEGATIVA) {
    // Mais tensão = MENOS temperatura. Trocar o sinal produz números
    // plausíveis e invertidos -- um aparelho esquentando pareceria esfriar,
    // que é exatamente a leitura que levaria à conclusão errada.
    const float frio = celsius_do_adc(bruto_para(0.750F));
    const float quente = celsius_do_adc(bruto_para(0.650F));
    EXPECT_LT(frio, 27.0F);
    EXPECT_GT(quente, 27.0F);
    EXPECT_LT(frio, quente);
}

TEST(Termometro, um_grau_corresponde_a_1721_microvolts) {
    // A escala, verificada e não suposta: 100 °C de diferença têm de sair de
    // 0,1721 V. Uma constante errada por um fator passaria nos dois testes
    // acima e daria temperatura absurda em campo.
    const float a = celsius_do_adc(bruto_para(0.706F));
    const float b = celsius_do_adc(bruto_para(0.706F - 0.1721F));
    EXPECT_NEAR(b - a, 100.0F, 1.0F);
}

TEST(Termometro, a_faixa_que_interessa_ao_painel_sai_plausivel) {
    // O painel mediu 92 °C (R-65) e o NEO-M8N e especificado ate 85 °C. A
    // conversao tem de produzir numeros nessa vizinhanca, e nao em outra
    // ordem de grandeza.
    const float t = celsius_do_adc(bruto_para(0.706F - 0.090F * 1.0F));
    EXPECT_GT(t, 50.0F);
    EXPECT_LT(t, 110.0F);
}

TEST(Termometro, leitura_zero_nao_estoura) {
    // ADC desligado ou canal errado devolve zero. Nao pode travar nem dar
    // NaN -- tem de dar um numero absurdo, que denuncia.
    const float t = celsius_do_adc(0);
    EXPECT_GT(t, 300.0F) << "zero deveria sair como temperatura impossivel";
}

TEST(Termometro, fundo_de_escala_tambem_nao_estoura) {
    const float t = celsius_do_adc(4095);
    EXPECT_LT(t, -1000.0F) << "fundo de escala deveria sair absurdo";
}

}  // namespace
