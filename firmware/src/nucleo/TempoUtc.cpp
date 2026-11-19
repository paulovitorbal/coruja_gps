#include "nucleo/TempoUtc.h"

namespace coruja {
namespace {

/// Antes disto o aparelho nao existia; depois, nao e problema dele. A faixa
/// larga e de proposito: ela separa "relogio zerado" de "relogio ajustado",
/// e nao tem pretensao de dizer se a hora esta CERTA.
constexpr int kAnoMinimo = 2020;
constexpr int kAnoMaximo = 2099;

constexpr std::int64_t kSegundosPorDia = 86400;

}  // namespace

bool TempoUtc::plausivel() const {
    return ano >= kAnoMinimo && ano <= kAnoMaximo &&
           mes >= 1 && mes <= 12 && dia >= 1 && dia <= 31 &&
           hora <= 23 && minuto <= 59 &&
           // 60 e segundo bissexto: o NMEA e o NTP podem entrega-lo, e
           // recusar a hora inteira por causa dele seria perder um segundo
           // por ano por preciosismo.
           segundo <= 60;
}

std::int32_t dias_desde_epoca(int ano, unsigned mes, unsigned dia) {
    ano -= mes <= 2 ? 1 : 0;
    const int era = (ano >= 0 ? ano : ano - 399) / 400;
    const auto yoe = static_cast<unsigned>(ano - era * 400);
    const unsigned doy = (153U * (mes + (mes > 2 ? -3U : 9U)) + 2U) / 5U + dia - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

void civil_de_dias(std::int32_t dias, int* ano, unsigned* mes, unsigned* dia) {
    // Em 64 bits porque o deslocamento da epoca NAO cabe de volta no int32:
    // `dias + 719468` estoura com sinal perto do fim da faixa, e o que saia
    // era uma data pequena e de aparencia normal. A funcao aceita qualquer
    // `int32_t`, e `de_segundos` lhe entrega ate o extremo.
    const std::int64_t desde_era_zero = static_cast<std::int64_t>(dias) + 719468;
    const std::int64_t era =
        (desde_era_zero >= 0 ? desde_era_zero : desde_era_zero - 146096) / 146097;
    const auto doe = static_cast<unsigned>(desde_era_zero - era * 146097);
    const unsigned yoe =
        (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    const int y = static_cast<int>(yoe) + static_cast<int>(era * 400);
    const unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    const unsigned mp = (5U * doy + 2U) / 153U;
    const unsigned d = doy - (153U * mp + 2U) / 5U + 1U;
    const unsigned m = mp + (mp < 10U ? 3U : -9U);

    if (ano != nullptr) { *ano = y + (m <= 2 ? 1 : 0); }
    if (mes != nullptr) { *mes = m; }
    if (dia != nullptr) { *dia = d; }
}

std::int64_t segundos_de(const TempoUtc& t) {
    const std::int64_t dias = dias_desde_epoca(t.ano, t.mes, t.dia);
    return dias * kSegundosPorDia +
           static_cast<std::int64_t>(t.hora) * 3600 +
           static_cast<std::int64_t>(t.minuto) * 60 +
           static_cast<std::int64_t>(t.segundo);
}

TempoUtc de_segundos(std::int64_t segundos) {
    // Divisao que arredonda para BAIXO, e nao para zero: antes de 1970 os
    // segundos sao negativos, e `/` em C++ truncaria na direcao errada --
    // 1969-12-31 viraria 1970-01-01 com hora negativa.
    std::int64_t dias = segundos / kSegundosPorDia;
    std::int64_t resto = segundos % kSegundosPorDia;
    if (resto < 0) {
        resto += kSegundosPorDia;
        --dias;
    }

    // Fora do que um int32 de dias representa nao ha o que devolver de util.
    // Zerado, o `plausivel()` recusa -- que e o comportamento certo para um
    // valor que nao deveria ter chegado aqui.
    if (dias < -2147483647LL || dias > 2147483647LL) { return {}; }

    int ano = 0;
    unsigned mes = 0;
    unsigned dia = 0;
    civil_de_dias(static_cast<std::int32_t>(dias), &ano, &mes, &dia);
    // Ano que nao cabe no campo daria um numero truncado e PLAUSIVEL: o ano
    // 67890 viraria 2354. Zerar devolve algo que o `plausivel()` recusa.
    if (ano < 0 || ano > 65535) { return {}; }

    TempoUtc t;
    t.ano = static_cast<std::uint16_t>(ano);
    t.mes = static_cast<std::uint8_t>(mes);
    t.dia = static_cast<std::uint8_t>(dia);
    t.hora = static_cast<std::uint8_t>(resto / 3600);
    t.minuto = static_cast<std::uint8_t>((resto % 3600) / 60);
    t.segundo = static_cast<std::uint8_t>(resto % 60);
    return t;
}

}  // namespace coruja
