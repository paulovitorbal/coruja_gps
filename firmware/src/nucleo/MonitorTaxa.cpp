#include "nucleo/MonitorTaxa.h"

namespace coruja {

const char* descreve(EstadoTaxa estado) {
    switch (estado) {
        case EstadoTaxa::Aquecendo: return "aquecendo";
        case EstadoTaxa::Nominal:   return "nominal";
        case EstadoTaxa::Degradado: return "degradado";
        case EstadoTaxa::Falha:     return "falha";
    }
    return "?";
}

void MonitorTaxa::reinicia() {
    inicio_ = 0;
    quantos_ = 0;
    houve_algum_ = false;
    abaixo_ = false;
    checksums_invalidos_ = 0;
    estado_ = EstadoTaxa::Aquecendo;
    taxa_hz_ = 0.0F;
    precisa_reconfigurar_ = false;
}

void MonitorTaxa::registra_checksum_invalido() { ++checksums_invalidos_; }

void MonitorTaxa::registra_fix(std::uint32_t agora_ms) {
    if (!houve_algum_) {
        primeiro_ms_ = agora_ms;
        houve_algum_ = true;
    }
    if (quantos_ == kCapacidadeJanela) {
        // Cheia: o mais antigo sai. Só acontece acima de 10,7 Hz, onde o
        // veredito é `Nominal` com ou sem a amostra perdida.
        inicio_ = (inicio_ + 1) % kCapacidadeJanela;
        --quantos_;
    }
    instantes_[(inicio_ + quantos_) % kCapacidadeJanela] = agora_ms;
    ++quantos_;
}

void MonitorTaxa::descarta_antigos(std::uint32_t agora_ms) {
    // Subtração em unsigned: correta através do estouro de 49 dias do
    // `to_ms_since_boot`. Não trocar por comparação de grandeza.
    while (quantos_ > 0 && (agora_ms - instantes_[inicio_]) >= kJanelaMs) {
        inicio_ = (inicio_ + 1) % kCapacidadeJanela;
        --quantos_;
    }
}

EstadoTaxa MonitorTaxa::avalia(std::uint32_t agora_ms) {
    descarta_antigos(agora_ms);
    taxa_hz_ = static_cast<float>(quantos_) /
               (static_cast<float>(kJanelaMs) / 1000.0F);

    // Aquecimento. Sem isto o arranque seria sempre uma falha: a janela de
    // 3 s começa vazia, e uma taxa perfeita de 4 Hz mede 0,33 Hz no primeiro
    // quarto de segundo. Um alarme que dispara sempre no boot é um alarme
    // que se aprende a ignorar.
    if (!houve_algum_ || (agora_ms - primeiro_ms_) < kJanelaMs) {
        estado_ = EstadoTaxa::Aquecendo;
        abaixo_ = false;
        return estado_;
    }

    if (taxa_hz_ >= kTaxaNominalHz) {
        abaixo_ = false;
        estado_ = EstadoTaxa::Nominal;
        return estado_;
    }
    if (taxa_hz_ >= kTaxaPisoHz) {
        abaixo_ = false;
        estado_ = EstadoTaxa::Degradado;
        return estado_;
    }

    // Abaixo do piso. Só vira falha se **sustentado**: o RF01.5 pede 5 s, e
    // um vale de meio segundo num túnel não é defeito de configuração.
    if (!abaixo_) {
        abaixo_ = true;
        abaixo_desde_ms_ = agora_ms;
    }
    if ((agora_ms - abaixo_desde_ms_) >= kSustentacaoFalhaMs) {
        if (estado_ != EstadoTaxa::Falha) {
            // Borda de entrada: arma o pedido de reconfiguração UMA vez.
            // Rearmar a cada ciclo faria o firmware reenviar a sequência UBX
            // quatro vezes por segundo contra um módulo que já não responde.
            precisa_reconfigurar_ = true;
        }
        estado_ = EstadoTaxa::Falha;
    } else {
        estado_ = EstadoTaxa::Degradado;
    }
    return estado_;
}

}  // namespace coruja
