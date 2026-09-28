#include "nucleo/SeletorPeriodo.h"

namespace coruja {

PeriodoDoDia SeletorPeriodo::atualiza(const Telemetria& t,
                                      std::uint32_t agora_ms) {
    // O modo forcado nao consulta o ceu. Nem precisaria do calculo, mas ele
    // segue em dia para que voltar a 'auto' nao deixe a tela no valor de
    // um minuto atras.
    if (!ja_calculou_ ||
        (agora_ms - ultimo_calculo_ms_) >= kIntervaloRecalculoMs) {
        calculado_ = periodo_do_dia(t);
        ultimo_calculo_ms_ = agora_ms;
        ja_calculou_ = true;
    }

    switch (modo_) {
        case ModoNoturno::SempreDia:   vigente_ = PeriodoDoDia::Dia; break;
        case ModoNoturno::SempreNoite: vigente_ = PeriodoDoDia::Noite; break;
        case ModoNoturno::Automatico:  vigente_ = calculado_; break;
    }
    return vigente_;
}

}  // namespace coruja
