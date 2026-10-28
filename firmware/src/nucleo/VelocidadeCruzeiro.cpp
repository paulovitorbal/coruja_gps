#include "nucleo/VelocidadeCruzeiro.h"

namespace coruja {

void VelocidadeCruzeiro::esquece() {
    tem_amostra_ = false;
    valida_ = false;
}

void VelocidadeCruzeiro::anota(float velocidade_kmh, std::uint32_t agora_ms) {
    if (velocidade_kmh < kVelocidadeMinimaCruzeiroKmh) {
        esquece();
        return;
    }

    // A subtração de `std::uint32_t` já é modular, então ela atravessa o
    // estouro do relógio sozinha — o mesmo que o descarte de alvo faz.
    const std::uint32_t desde_a_ultima = agora_ms - ultimo_ms_;

    if (!tem_amostra_ || desde_a_ultima > kBuracoMaximoCruzeiroMs) {
        tem_amostra_ = true;
        valida_ = false;
        media_kmh_ = velocidade_kmh;
        desde_ms_ = agora_ms;
        ultimo_ms_ = agora_ms;
        return;
    }

    ultimo_ms_ = agora_ms;

    // α = Δt / (τ + Δt): a forma da média exponencial que não depende do
    // intervalo entre amostras. Com Δt fixo ela vira a constante de sempre;
    // com Δt variável, que é o caso quando a taxa do GPS oscila, ela mantém a
    // constante de tempo em segundos em vez de em amostras.
    const float dt = static_cast<float>(desde_a_ultima);
    const float alfa = dt / (static_cast<float>(kTauCruzeiroMs) + dt);
    media_kmh_ += alfa * (velocidade_kmh - media_kmh_);

    if ((agora_ms - desde_ms_) >= kAquecimentoCruzeiroMs) { valida_ = true; }
}

}  // namespace coruja
