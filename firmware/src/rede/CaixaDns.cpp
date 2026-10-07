#include "rede/CaixaDns.h"

namespace coruja {

std::uint32_t CaixaDns::abre() {
    ++ultima_;
    // `ultima_` so cresce. Zera-la ao abandonar faria a geracao seguinte
    // repetir uma ja usada, e uma resposta atrasada daquela seria aceita como
    // sendo desta -- exatamente o que a geracao existe para impedir.
    if (ultima_ == 0) { ultima_ = 1; }
    em_curso_ = ultima_;
    pronto_ = false;
    achou_ = false;
    return em_curso_;
}

void CaixaDns::abandona() { em_curso_ = 0; }

bool CaixaDns::entrega(std::uint32_t geracao, bool achou) {
    if (geracao == 0 || geracao != em_curso_) { return false; }
    pronto_ = true;
    achou_ = achou;
    return true;
}

}  // namespace coruja
