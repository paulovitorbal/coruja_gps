#include "led/PadraoLed.h"

namespace coruja {
namespace {

/// Em que metade do ciclo estamos. Subtração em unsigned atravessa o estouro
/// de 49 dias; o módulo mantém o valor dentro de um período.
bool primeira_metade(std::uint32_t decorrido, std::uint32_t periodo) {
    return (decorrido % periodo) < (periodo / 2);
}

}  // namespace

void PadraoLed::define_zona(Zona zona, std::uint32_t agora_ms) {
    if (zona == zona_) { return; }
    zona_ = zona;
    inicio_ = agora_ms;
}

Cor PadraoLed::cor(std::uint32_t agora_ms) const {
    const std::uint32_t decorrido = agora_ms - inicio_;
    switch (zona_) {
        case Zona::SemSinal:
            // Apagado, e não verde: verde dentro de um túnel diria "não há
            // radar por perto", que é justamente o que não se sabe.
            return cores::kApagado;

        case Zona::Segura:
            return cores::kVerde;

        case Zona::AproximacaoConforme:
            return cores::kAmarelo;

        case Zona::AproximacaoMargem:
            return primeira_metade(decorrido, kPeriodoMargemMs)
                       ? cores::kRosa : cores::kApagado;

        case Zona::Perigo:
            return primeira_metade(decorrido, kPeriodoPerigoMs)
                       ? cores::kVermelho : cores::kApagado;

        case Zona::Semaforo:
            // Alterna entre duas cores em vez de piscar contra o apagado: o
            // semáforo não é gravidade intermediária, é outra natureza de
            // alerta, e o par amarelo/vermelho evoca a própria coisa.
            return primeira_metade(decorrido, kPeriodoSemaforoMs)
                       ? cores::kAmarelo : cores::kVermelho;
    }
    return cores::kApagado;
}

}  // namespace coruja
