#pragma once
#include <cstdint>

namespace coruja {

/// A temperatura da PASTILHA do RP2350, em graus Celsius.
///
/// ⚠️ **Não é a temperatura do ar, nem a do módulo GPS.** O sensor fica dentro
/// do próprio chip e corre acima do ambiente por autoaquecimento. O que ele
/// mede bem é a **tendência**: subiu, estabilizou, desceu. Tratar o número
/// como temperatura do painel levaria a conclusão errada.
///
/// Existe por um caso concreto: em 08/10/2026 o aparelho ficou ~20 min sem fix
/// depois de o carro parar e voltar a andar, e a temperatura é uma das
/// suspeitas. O painel mediu 92 °C (R-65), e o NEO-M8N é especificado até
/// 85 °C — mas ninguém mediu o que acontece dentro do gabinete. Isto é o
/// instrumento, não a resposta.
///
/// O canal 4 do ADC é o sensor interno no RP2350 QFN-60, que é o do Pico 2 W.

/// Converte a leitura crua de 12 bits em graus Celsius.
///
/// Pela fórmula do datasheet, que o SDK repete em `hardware/adc.h`:
///
///     T = 27 − (V − 0,706) / 0,001721
///
/// com `V = bruto × 3,3 / 4095`.
///
/// Separada da leitura de propósito: a conversão é onde o erro mora — a
/// referência, a largura do conversor, o sinal da inclinação — e é ela que
/// roda no host. Ler o registrador não tem o que errar.
float celsius_do_adc(std::uint16_t bruto);

/// Lê o sensor interno. `false` quando não há sensor (alvo de host).
///
/// A primeira leitura depois de ligar o sensor pode sair alta; quem chama em
/// intervalo longo não precisa se importar.
bool temperatura_do_chip(float* celsius);

}  // namespace coruja
