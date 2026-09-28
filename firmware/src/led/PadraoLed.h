#pragma once
#include <cstdint>

#include "led/Cor.h"
#include "nucleo/Zonamento.h"

namespace coruja {

/// Períodos de piscada do RF03, em milissegundos.
constexpr std::uint32_t kPeriodoMargemMs = 1000;    // 1 Hz
constexpr std::uint32_t kPeriodoPerigoMs = 250;     // 4 Hz
constexpr std::uint32_t kPeriodoSemaforoMs = 500;   // 2 Hz, alternando

/// Traduz a zona em cor e piscada.
///
/// Fica separado do `Zonamento` de propósito: a máquina decide **o que está
/// acontecendo**, e isto decide **como mostrar**. Trocar a paleta, o período
/// ou o LED não deve tocar na lógica de alerta.
///
/// Sem relógio próprio, como o resto: o instante entra por parâmetro.
class PadraoLed {
public:
    /// Informa a zona vigente. **Idempotente** — repetir a mesma zona não
    /// reinicia a fase, pelo mesmo motivo do `CadenciaBuzzer`: o laço chama
    /// isto a cada volta, e reiniciar deixaria o LED preso aceso.
    void define_zona(Zona zona, std::uint32_t agora_ms);

    /// Cor que o LED deve mostrar agora.
    Cor cor(std::uint32_t agora_ms) const;

    Zona zona() const { return zona_; }

private:
    Zona          zona_ = Zona::SemSinal;
    std::uint32_t inicio_ = 0;
};

}  // namespace coruja
