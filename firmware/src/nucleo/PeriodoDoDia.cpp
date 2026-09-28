#include "nucleo/PeriodoDoDia.h"

#include <cmath>

namespace coruja {
namespace {

constexpr float kPi = 3.14159265F;
constexpr float kGraus = 180.0F / kPi;
/// Zênite do nascer e do pôr: 90° mais a refração atmosférica e meio disco
/// solar. É o que faz o dia durar um pouco mais que a geometria pura.
constexpr float kZenite = 90.833F;

float graus_normalizados(float g) {
    g = std::fmod(g, 360.0F);
    return g < 0.0F ? g + 360.0F : g;
}

/// Dias desde J2000. O número juliano tem sete dígitos e **não cabe em
/// `float`** com precisão útil; a subtração fica em inteiro, e só o resto
/// pequeno vai para ponto flutuante.
long dias_desde_j2000(int ano, int mes, int dia) {
    const int a = (14 - mes) / 12;
    const long y = ano + 4800L - a;
    const long m = mes + 12L * a - 3;
    const long jdn = dia + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 +
                     y / 400 - 32045;
    return jdn - 2451545L;
}

}  // namespace

const char* descreve(PeriodoDoDia p) {
    switch (p) {
        case PeriodoDoDia::Desconhecido: return "desconhecido";
        case PeriodoDoDia::Dia:          return "dia";
        case PeriodoDoDia::Noite:        return "noite";
    }
    return "?";
}

Crepusculo crepusculo(int ano, int mes, int dia, float lat, float lon) {
    Crepusculo saida;
    const float d = static_cast<float>(dias_desde_j2000(ano, mes, dia)) + 0.5F;

    const float g = graus_normalizados(357.529F + 0.98560028F * d) / kGraus;
    const float q = graus_normalizados(280.459F + 0.98564736F * d);
    const float lambda =
        graus_normalizados(q + 1.915F * std::sin(g) + 0.020F * std::sin(2 * g)) /
        kGraus;
    const float e = (23.439F - 0.00000036F * d) / kGraus;

    const float decl = std::asin(std::sin(e) * std::sin(lambda));
    const float ra = graus_normalizados(
        std::atan2(std::cos(e) * std::sin(lambda), std::cos(lambda)) * kGraus);
    // Equação do tempo: a diferença entre o sol médio e o sol de verdade.
    // Chega a ±16 minutos ao longo do ano, e ignorá-la erraria o crepúsculo
    // em mais do que a precisão que se quer aqui.
    float eqt = std::fmod(q - ra + 180.0F, 360.0F) - 180.0F;

    const float phi = lat / kGraus;
    const float cos_h = (std::cos(kZenite / kGraus) -
                         std::sin(phi) * std::sin(decl)) /
                        (std::cos(phi) * std::cos(decl));
    if (cos_h > 1.0F) {
        // O sol não nasce: noite polar. Não é erro, é latitude alta.
        saida.valido = false;
        saida.sol_sempre_acima = false;
        return saida;
    }
    if (cos_h < -1.0F) {
        saida.valido = false;
        saida.sol_sempre_acima = true;
        return saida;
    }

    const float h = std::acos(cos_h) * kGraus;
    const float meio_dia = 720.0F - 4.0F * (lon + eqt);
    // Normaliza para [0, 1440). Em longitudes a leste o meio-dia solar cai
    // perto de 00:00 UTC e o nascer sai NEGATIVO; a oeste o pôr passa de
    // 1440. Sem isto a comparação com o relógio compara coisas diferentes,
    // e o aparelho acha que é dia à meia-noite na Nova Zelândia.
    auto no_dia = [](float m) {
        int v = static_cast<int>(std::floor(m + 0.5F)) % 1440;
        return v < 0 ? v + 1440 : v;
    };
    saida.nascer_min = no_dia(meio_dia - 4.0F * h);
    saida.por_min = no_dia(meio_dia + 4.0F * h);
    saida.valido = true;
    return saida;
}

PeriodoDoDia periodo_do_dia(const Telemetria& t) {
    // Sem data não há como saber, e chutar seria pior: o brilho mudaria
    // sozinho no boot, antes do primeiro fix, e voltaria ao mudar.
    if (!t.data_valida) { return PeriodoDoDia::Desconhecido; }

    const Crepusculo c = crepusculo(static_cast<int>(t.ano),
                                    static_cast<int>(t.mes),
                                    static_cast<int>(t.dia), t.lat, t.lon);
    if (!c.valido) {
        return c.sol_sempre_acima ? PeriodoDoDia::Dia : PeriodoDoDia::Noite;
    }

    const int agora = static_cast<int>(t.hora) * 60 + static_cast<int>(t.minuto);
    // O intervalo de dia pode cruzar a meia-noite UTC dependendo da
    // longitude — no Brasil não cruza, mas o código não é do Brasil.
    const bool dia = (c.nascer_min <= c.por_min)
                         ? (agora >= c.nascer_min && agora < c.por_min)
                         : (agora >= c.nascer_min || agora < c.por_min);
    return dia ? PeriodoDoDia::Dia : PeriodoDoDia::Noite;
}

}  // namespace coruja
