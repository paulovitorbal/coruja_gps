// Ponto de entrada. Por enquanto e um smoke test do alvo: prova que o nucleo
// compila e roda no RP2350, e reporta o orcamento de memoria real da base.
//
// O que falta para virar firmware de verdade esta na tabela de estado do
// README: leitor SD, GPS, display, LED, buzzer e encoder.
#include <pico/stdlib.h>

#include <cstdio>

#include "log/LoggerConsole.h"
#include "nucleo/BaseRadares.h"
#include "nucleo/LimiarInfracao.h"

namespace {

/// A base vive em `.bss`, dimensionada pelo teto do formato. Nao ha alocacao
/// dinamica em nenhum ponto do caminho critico.
coruja::Ponto g_pontos[coruja::kTetoPontos];

}  // namespace

int main() {
    stdio_init_all();
    coruja::LoggerConsole log(nullptr, coruja::Nivel::Debug);

    // Dar tempo ao host de abrir o terminal antes do primeiro log.
    sleep_ms(2000);
    log.info("boot", "Coruja GPS — smoke test do alvo");

    char msg[96];
    std::snprintf(msg, sizeof msg, "base reservada: %u pontos, %u B em .bss",
                  static_cast<unsigned>(coruja::kTetoPontos),
                  static_cast<unsigned>(sizeof g_pontos));
    log.info("mem", msg);

    // Exercita a logica pura no alvo, para confirmar que a FPU de precisao
    // simples produz os mesmos numeros que o host.
    std::snprintf(msg, sizeof msg, "V_infra(80) = %.1f  V_infra(110) = %.1f",
                  static_cast<double>(coruja::velocidade_infracao(80)),
                  static_cast<double>(coruja::velocidade_infracao(110)));
    log.info("limiar", msg);

    log.warning("boot", "sem base carregada: leitor SD ainda nao implementado");

    while (true) {
        tight_loop_contents();
    }
}
