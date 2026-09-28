#pragma once
#include <cstdint>

#include "nucleo/Configuracao.h"
#include "nucleo/PeriodoDoDia.h"

namespace coruja {

/// De quanto em quanto tempo o nascer e o por do sol sao recalculados.
///
/// A fronteira entre dia e noite se move em minutos; um minuto de atraso
/// para trocar o preset de brilho e invisivel. O calculo do NOAA e todo em
/// ponto flutuante, e o RP2350 so tem FPU de precisao simples -- refaze-lo
/// a 4 Hz seria gastar o laco do alerta com uma conta que nao muda.
constexpr std::uint32_t kIntervaloRecalculoMs = 60000;

/// Decide o periodo vigente: o calculo do ceu, com o ajuste do arquivo por
/// cima.
///
/// `SempreDia` e `SempreNoite` existem para o caso que o calculo nao ve --
/// a garagem coberta ao meio-dia, onde a tela clara e inutil, e a rua bem
/// iluminada de madrugada. E por isso que sao forcados: se dependessem do
/// ceu nao serviriam para nada.
class SeletorPeriodo {
public:
    void define_modo(ModoNoturno m) { modo_ = m; }

    /// Uma volta do laco. Devolve o periodo vigente.
    PeriodoDoDia atualiza(const Telemetria& t, std::uint32_t agora_ms);

    PeriodoDoDia periodo() const { return vigente_; }

private:
    ModoNoturno modo_ = ModoNoturno::Automatico;
    PeriodoDoDia calculado_ = PeriodoDoDia::Desconhecido;
    PeriodoDoDia vigente_ = PeriodoDoDia::Desconhecido;
    std::uint32_t ultimo_calculo_ms_ = 0;
    bool ja_calculou_ = false;
};

}  // namespace coruja
