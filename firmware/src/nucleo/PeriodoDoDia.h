#pragma once
#include <cstdint>

#include "nucleo/Nmea.h"

namespace coruja {

enum class PeriodoDoDia : std::uint8_t {
    Desconhecido,   ///< sem fix, ou sem data: não se sabe, e não se chuta
    Dia,
    Noite,
};

const char* descreve(PeriodoDoDia p);

/// Nascer e pôr do sol, em minutos desde a meia-noite **UTC**.
struct Crepusculo {
    int  nascer_min = 0;
    int  por_min = 0;
    bool valido = false;   ///< falso no dia ou na noite polar
    bool sol_sempre_acima = false;
};

/// Calcula nascer e pôr do sol pela posição e pela data.
///
/// Algoritmo do NOAA, com o zênite de 90,833° que inclui refração atmosférica
/// e o raio aparente do disco solar — é por isso que o dia dura ~12h07m no
/// equinócio, e não 12h exatas.
///
/// **Por que não um LDR** (R-61): o aparelho já tem data, latitude e
/// longitude, então o dia e a noite saem de graça — sem peça, sem ADC, sem
/// calibração, sem furo na caixa. E para escolher um *modo* isto é mais
/// confiável que um sensor: não vira noite dentro de um túnel.
Crepusculo crepusculo(int ano, int mes, int dia, float lat, float lon);

/// O período, a partir da telemetria. `Desconhecido` enquanto não houver
/// data válida — o que, sem RTC, dura até o primeiro fix.
PeriodoDoDia periodo_do_dia(const Telemetria& t);

}  // namespace coruja
