#include "nucleo/RetomadaViagem.h"

#include <cstdio>

#include "nucleo/TempoUtc.h"

namespace coruja {

namespace {

// `dias_desde_epoca` vive em `TempoUtc.h` desde 2026-10-06. Estava aqui
// dentro, privado; a sincronizacao de hora precisou do mesmo algoritmo e
// copia-lo seria garantir que as duas versoes divergissem.

bool data_plausivel(const EstadoViagemSalvo& e) {
    return e.ano >= 2020 && e.ano <= 2099 && e.mes >= 1 && e.mes <= 12 &&
           e.dia >= 1 && e.dia <= 31 && e.hora <= 23 && e.minuto <= 59;
}

}  // namespace

std::int32_t minutos_utc(std::uint16_t ano, std::uint8_t mes, std::uint8_t dia,
                         std::uint8_t hora, std::uint8_t minuto) {
    return dias_desde_epoca(ano, mes, dia) * 1440 +
           static_cast<std::int32_t>(hora) * 60 +
           static_cast<std::int32_t>(minuto);
}

bool pode_retomar(const EstadoViagemSalvo& salvo, const Telemetria& agora) {
    if (!salvo.ativa || !agora.data_valida || !data_plausivel(salvo)) {
        return false;
    }
    const std::int32_t t0 =
        minutos_utc(salvo.ano, salvo.mes, salvo.dia, salvo.hora, salvo.minuto);
    const std::int32_t t1 =
        minutos_utc(agora.ano, agora.mes, agora.dia, agora.hora, agora.minuto);
    // **Aritmética com sinal, de propósito.** A primeira versão comparava
    // `static_cast<uint32_t>(t1 - t0)`, e aí o relógio para trás virava um
    // número enorme que reprovava a janela por acidente — a guarda abaixo
    // nunca executava, e a mutação mostrou isso. Proteção que depende de
    // wraparound some calada no dia em que alguém "arruma" o cast.
    const std::int32_t parada_min = t1 - t0;
    if (parada_min < 0) {
        return false;  // relógio para trás: dado corrompido ou outro cartão
    }
    return parada_min <= static_cast<std::int32_t>(kJanelaRetomadaMin);
}

std::size_t formata_estado(const EstadoViagemSalvo& e, char* destino,
                           std::size_t capacidade) {
    if (destino == nullptr || capacidade == 0) { return 0; }
    const int n = std::snprintf(
        destino, capacidade,
        "ativa=%u\nutc=%04u-%02u-%02uT%02u:%02uZ\ndist_km=%.2f\n",
        e.ativa ? 1U : 0U, static_cast<unsigned>(e.ano),
        static_cast<unsigned>(e.mes), static_cast<unsigned>(e.dia),
        static_cast<unsigned>(e.hora), static_cast<unsigned>(e.minuto),
        static_cast<double>(e.dist_km));
    if (n < 0 || static_cast<std::size_t>(n) >= capacidade) { return 0; }
    return static_cast<std::size_t>(n);
}

bool analisa_estado(const char* texto, std::size_t tamanho,
                    EstadoViagemSalvo* destino) {
    if (texto == nullptr || destino == nullptr || tamanho == 0) {
        return false;
    }
    // Cópia local para garantir o terminador: o chamador entrega o que leu do
    // cartão, e nada obriga que venha terminado.
    char buf[kTamEstadoViagem] = {};
    if (tamanho >= sizeof buf) { return false; }
    for (std::size_t i = 0; i < tamanho; ++i) { buf[i] = texto[i]; }

    unsigned ativa = 0;
    unsigned ano = 0, mes = 0, dia = 0, hora = 0, minuto = 0;
    double dist = 0.0;
    const int campos = std::sscanf(
        buf, "ativa=%u utc=%u-%u-%uT%u:%uZ dist_km=%lf",
        &ativa, &ano, &mes, &dia, &hora, &minuto, &dist);
    if (campos != 7) { return false; }

    EstadoViagemSalvo lido;
    lido.ativa = ativa != 0;
    lido.ano = static_cast<std::uint16_t>(ano);
    lido.mes = static_cast<std::uint8_t>(mes);
    lido.dia = static_cast<std::uint8_t>(dia);
    lido.hora = static_cast<std::uint8_t>(hora);
    lido.minuto = static_cast<std::uint8_t>(minuto);
    lido.dist_km = static_cast<float>(dist);
    if (!data_plausivel(lido)) { return false; }

    *destino = lido;
    return true;
}

}  // namespace coruja
