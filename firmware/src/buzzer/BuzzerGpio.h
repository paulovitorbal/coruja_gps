#pragma once
#include "buzzer/Buzzer.h"

namespace coruja {

/// Porte do buzzer no RP2350: um GPIO que chaveia a base do `BC337` através
/// do `R4` de 1 kΩ, e o transistor fecha o retorno de 12 V pelo coletor.
///
/// O GPIO **não** alimenta o buzzer: 3,3 V não fecham 12 V, e os ~50 mA do
/// SFM-27 passam do que o pino entrega — é o RNF02, e é por isso que existe o
/// transistor. O GPIO só decide.
class BuzzerGpio : public Buzzer {
public:
    /// Configura o pino e deixa o buzzer **calado**. O estado inicial
    /// importa: um GPIO recém-inicializado em nível alto apitaria desde o
    /// boot até a primeira decisão do laço.
    BuzzerGpio();

    void define(bool ligado) override;
    bool ligado() const override { return ligado_; }

private:
    bool ligado_ = false;
};

}  // namespace coruja
