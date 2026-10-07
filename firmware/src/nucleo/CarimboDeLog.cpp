#include "nucleo/CarimboDeLog.h"

#include <cstdio>

#include "nucleo/TempoUtc.h"

namespace coruja {

void formata_carimbo(std::int64_t utc_s, std::uint32_t ms_desde_boot,
                     char* destino, std::size_t capacidade) {
    if (destino == nullptr || capacidade == 0) { return; }
    destino[0] = '\0';
    if (capacidade < kTamCarimbo) { return; }

    // `plausivel()` sozinho basta, e isto foi MEDIDO: uma campanha de mutacao
    // mostrou que o `utc_s <= 0` que havia aqui era inalcancavel. O contrato
    // dele e justamente este -- "nao e validacao de calendario, e deteccao de
    // relogio nao ajustado" --, e tanto o zero (que vira 1970) quanto um
    // negativo (que o `de_segundos` zera) caem fora da faixa 2020-2099.
    const TempoUtc t = de_segundos(utc_s);
    if (!t.plausivel()) {
        // Sem hora de parede. Oito digitos cobrem 27 horas de ligacao
        // continua; passando disso o numero cresce e o campo tambem -- e
        // perder o alinhamento e melhor do que truncar o tempo.
        std::snprintf(destino, capacidade, "+%08lu",
                      static_cast<unsigned long>(ms_desde_boot));
        return;
    }
    std::snprintf(destino, capacidade, "%04u-%02u-%02uT%02u:%02u:%02uZ",
                  static_cast<unsigned>(t.ano), static_cast<unsigned>(t.mes),
                  static_cast<unsigned>(t.dia), static_cast<unsigned>(t.hora),
                  static_cast<unsigned>(t.minuto),
                  static_cast<unsigned>(t.segundo));
}

}  // namespace coruja
