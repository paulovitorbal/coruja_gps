#pragma once

namespace coruja::geo {

/// Metros por grau de latitude. Constante para o proposito: a variacao com a
/// latitude e de ~1% entre o equador e os polos, irrelevante contra a
/// incerteza do proprio fix (formato_dados.md §4.1).
constexpr float kMetrosPorGrauLat = 111320.0F;

/// Distancia equirretangular em metros, nao Haversine.
///
/// A FPU do RP2350 e de precisao simples e nao tem funcoes transcendentais em
/// hardware: `sin`/`cos`/`asin` viriam da biblioteca de software. Nas
/// distancias que importam (ate 300 m) o erro da aproximacao equirretangular
/// fica na casa de centimetros, muito abaixo da incerteza do GPS.
///
/// O cosseno da latitude e passado pelo chamador porque ele muda devagar e
/// pode ser recalculado uma vez por ciclo, nao uma vez por ponto.
float distancia_m(float lat_a, float lon_a, float lat_b, float lon_b,
                  float cos_lat);

/// Diferenca angular minima entre dois rumos, em graus, sempre em [0, 180].
///
/// Resolve o R-04: uma subtracao simples erra no cruzamento de 0/360 graus,
/// tratando 359 e 1 como 358 graus de diferenca em vez de 2.
float diferenca_rumo(float rumo_a, float rumo_b);

/// Cosseno da latitude, para reaproveitar entre pontos do mesmo ciclo.
float cosseno_latitude(float lat_graus);

}  // namespace coruja::geo
