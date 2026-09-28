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
            // **Azul fixo**, e nem verde nem apagado.
            //
            // Verde diria "não há radar por perto", que é justamente o que
            // não se sabe dentro de um túnel. E apagado, que era o desenho
            // original, significava duas coisas ao mesmo tempo: "perdi o
            // GPS" e "o aparelho morreu". Com o azul, o escuro passa a ter
            // **um** significado só, e o motorista distingue sem tirar os
            // olhos da estrada.
            //
            // Fixo e não pulsante de propósito: sem fix não há ação a tomar,
            // e piscar pediria uma atenção que não se deve pedir. O canal
            // pulsante existe para o que é urgente.
            //
            // Azul é o matiz mais distante de tudo que já se usa — 96° do
            // vizinho mais próximo, contra os 40° que o §4.1 exige.
            return cores::kAzul;

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
