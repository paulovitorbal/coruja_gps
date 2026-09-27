#include "buzzer/CadenciaBuzzer.h"

namespace coruja {

void CadenciaBuzzer::define_faixa(FaixaSonora faixa, std::uint32_t agora_ms) {
    if (faixa == faixa_) {
        return;   // ver a nota de idempotência no cabeçalho
    }
    faixa_ = faixa;
    inicio_ = agora_ms;
    // A troca de faixa recomeça o padrão em vez de esperar o ciclo corrente
    // fechar. Vindo de `Lenta`, esperar custaria até 900 ms para o motorista
    // ouvir que a situação piorou — e é justamente quando ele precisa saber.
    ligado_ = padrao_de(faixa_).ligado_ms > 0;
}

void CadenciaBuzzer::silencia() {
    faixa_ = FaixaSonora::Nenhuma;
    ligado_ = false;
}

bool CadenciaBuzzer::atualiza(std::uint32_t agora_ms) {
    const PadraoSonoro p = padrao_de(faixa_);
    // Silêncio tem período **zero**, e esta guarda é o que impede o resto da
    // função de dividir por ele. Não é defesa decorativa: sem ela, a faixa
    // `Nenhuma` — que é o estado normal na maior parte do tempo — derruba o
    // firmware no primeiro `%`.
    if (p.periodo_ms == 0 || p.ligado_ms == 0) {
        ligado_ = false;
        return false;
    }
    // Subtração em unsigned: correta através do estouro de 49 dias do
    // `to_ms_since_boot`. Avançar `inicio_` por ciclos inteiros mantém a
    // diferença sempre menor que um período, então a fase não escorrega e o
    // estouro nunca chega a aparecer no cálculo.
    std::uint32_t decorrido = agora_ms - inicio_;
    if (decorrido >= p.periodo_ms) {
        const std::uint32_t ciclos = decorrido / p.periodo_ms;
        inicio_ += ciclos * p.periodo_ms;
        decorrido -= ciclos * p.periodo_ms;
    }
    ligado_ = decorrido < p.ligado_ms;
    return ligado_;
}

}  // namespace coruja
