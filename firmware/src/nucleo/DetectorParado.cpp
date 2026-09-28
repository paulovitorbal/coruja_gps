#include "nucleo/DetectorParado.h"

namespace coruja {

void DetectorParado::conta(std::uint32_t agora_ms) {
    if (!contando_) {
        contando_ = true;
        desde_ms_ = agora_ms;
    }
    parado_ = (agora_ms - desde_ms_) >= kSustentacaoParadoMs;
}

void DetectorParado::atualiza(float velocidade_kmh, std::uint32_t agora_ms) {
    teve_fix_ = true;
    if (velocidade_kmh < kVelocidadeParadoKmh) {
        conta(agora_ms);
        return;
    }
    // Acima do limiar o veredito cai na hora. A sustentacao existe para
    // *entrar* em parado, nunca para atrasar a saida: sair tarde e o que
    // deixaria o menu aberto com o carro ja andando.
    contando_ = false;
    parado_ = false;
}

void DetectorParado::sem_fix(std::uint32_t agora_ms) {
    if (teve_fix_) {
        return;  // congela o veredito: o tunel nao para o carro
    }
    conta(agora_ms);
}

void DetectorParado::reinicia() {
    teve_fix_ = false;
    parado_ = false;
    contando_ = false;
    desde_ms_ = 0;
}

}  // namespace coruja
