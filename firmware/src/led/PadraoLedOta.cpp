#include "led/PadraoLedOta.h"

namespace coruja {

namespace {

constexpr std::uint32_t kPeriodoLentoMs = 1000;   // 1 Hz
constexpr std::uint32_t kPeriodoRapidoMs = 500;   // 2 Hz

/// Metade do período aceso, metade apagado.
bool aceso(std::uint32_t decorrido, std::uint32_t periodo) {
    return (decorrido % periodo) < (periodo / 2);
}

}  // namespace

void PadraoLedOta::define_fase(FaseOta fase, std::uint32_t agora_ms) {
    if (fase == fase_) {
        return;  // idempotente: o observador avisa a mesma fase varias vezes
    }
    fase_ = fase;
    desde_ms_ = agora_ms;
}

Cor PadraoLedOta::cor(std::uint32_t agora_ms) const {
    const std::uint32_t decorrido = agora_ms - desde_ms_;
    switch (fase_) {
        case FaseOta::Conectando:
        case FaseOta::Consultando:
            return aceso(decorrido, kPeriodoLentoMs) ? cores::kCiano
                                                     : cores::kApagado;
        case FaseOta::Baixando:
            return aceso(decorrido, kPeriodoRapidoMs) ? cores::kCiano
                                                      : cores::kApagado;
        case FaseOta::Verificando:
        case FaseOta::Gravando:
            // **Fixo de proposito.** E a janela da troca atomica do RF05.2,
            // o unico momento em que desligar tem consequencia. Luz parada
            // se le como "ocupado"; piscando, convida a mexer.
            return cores::kCiano;
        case FaseOta::Concluida:
        case FaseOta::JaEmDia:
            return cores::kVerde;
        case FaseOta::Falhou:
            return aceso(decorrido, kPeriodoRapidoMs) ? cores::kVermelho
                                                      : cores::kApagado;
    }
    return cores::kApagado;
}

}  // namespace coruja
