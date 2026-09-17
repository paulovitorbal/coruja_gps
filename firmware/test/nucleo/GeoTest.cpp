#include "nucleo/Geo.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

using namespace coruja;

// --- diferenca_rumo: o R-04 era exatamente o wraparound de 0/360 ---

TEST(DiferencaRumo, RumosIguaisDaoZero) {
    EXPECT_FLOAT_EQ(geo::diferenca_rumo(90.0F, 90.0F), 0.0F);
}

TEST(DiferencaRumo, CruzaZeroSemErrar) {
    // Uma subtracao simples daria 358, e o filtro de sentido rejeitaria um
    // radar que esta na mesma direcao. Este e o bug do R-04.
    EXPECT_FLOAT_EQ(geo::diferenca_rumo(359.0F, 1.0F), 2.0F);
    EXPECT_FLOAT_EQ(geo::diferenca_rumo(1.0F, 359.0F), 2.0F);
    EXPECT_FLOAT_EQ(geo::diferenca_rumo(350.0F, 10.0F), 20.0F);
}

TEST(DiferencaRumo, OpostosDao180) {
    EXPECT_FLOAT_EQ(geo::diferenca_rumo(0.0F, 180.0F), 180.0F);
    EXPECT_FLOAT_EQ(geo::diferenca_rumo(270.0F, 90.0F), 180.0F);
}

TEST(DiferencaRumo, NuncaPassaDe180) {
    for (float a = 0.0F; a < 360.0F; a += 7.0F) {
        for (float b = 0.0F; b < 360.0F; b += 11.0F) {
            const float d = geo::diferenca_rumo(a, b);
            EXPECT_GE(d, 0.0F) << a << " vs " << b;
            EXPECT_LE(d, 180.0F) << a << " vs " << b;
        }
    }
}

TEST(DiferencaRumo, ESimetrica) {
    EXPECT_FLOAT_EQ(geo::diferenca_rumo(37.0F, 315.0F),
                    geo::diferenca_rumo(315.0F, 37.0F));
}

// --- distancia_m ---

TEST(Distancia, PontoParaSiMesmoDaZero) {
    const float lat = -15.7801F;
    const float c   = geo::cosseno_latitude(lat);
    EXPECT_FLOAT_EQ(geo::distancia_m(lat, -47.9292F, lat, -47.9292F, c), 0.0F);
}

TEST(Distancia, UmGrauDeLatitudeDaMetrosPorGrau) {
    const float c = geo::cosseno_latitude(0.0F);
    EXPECT_NEAR(geo::distancia_m(0.0F, 0.0F, 1.0F, 0.0F, c),
                geo::kMetrosPorGrauLat, 1.0F);
}

TEST(Distancia, LongitudeEncolheComALatitude) {
    // Em Brasilia (-15,78 graus) um grau de longitude vale cos(lat) do que
    // vale no equador. E por isso que o cosseno entra na conta.
    const float lat = -15.7801F;
    const float c   = geo::cosseno_latitude(lat);
    const float d   = geo::distancia_m(lat, 0.0F, lat, 1.0F, c);
    EXPECT_NEAR(d, geo::kMetrosPorGrauLat * std::cos(lat * 0.017453292F), 1.0F);
}

TEST(Distancia, TrezentosMetrosAoNorteSaoTrezentosMetros) {
    const float lat  = -15.7801F;
    const float c    = geo::cosseno_latitude(lat);
    const float dlat = 300.0F / geo::kMetrosPorGrauLat;
    EXPECT_NEAR(geo::distancia_m(lat, -47.9292F, lat + dlat, -47.9292F, c),
                300.0F, 0.5F);
}

TEST(Distancia, ESimetrica) {
    const float c = geo::cosseno_latitude(-15.0F);
    EXPECT_FLOAT_EQ(geo::distancia_m(-15.0F, -47.0F, -15.1F, -47.1F, c),
                    geo::distancia_m(-15.1F, -47.1F, -15.0F, -47.0F, c));
}

TEST(Distancia, ErroContraHaversineEIrrelevanteAte300m) {
    // A justificativa de trocar Haversine por equirretangular (R-03) e que o
    // erro nessa escala fica muito abaixo da incerteza do GPS.
    const float lat = -15.7801F;
    const float lon = -47.9292F;
    const float c   = geo::cosseno_latitude(lat);
    const double R  = 6371000.0;
    const double g  = 0.017453292519943295;

    for (float m = 50.0F; m <= 300.0F; m += 50.0F) {
        const float d = m / geo::kMetrosPorGrauLat;
        const float lat_b = lat + d * 0.7F;
        const float lon_b = lon + d * 0.7F;

        const double p1 = lat * g, p2 = lat_b * g;
        const double dp = (lat_b - lat) * g, dl = (lon_b - lon) * g;
        const double a  = std::sin(dp / 2) * std::sin(dp / 2) +
                          std::cos(p1) * std::cos(p2) *
                          std::sin(dl / 2) * std::sin(dl / 2);
        const double hav = 2 * R * std::asin(std::sqrt(a));

        EXPECT_NEAR(geo::distancia_m(lat, lon, lat_b, lon_b, c), hav, 1.0)
            << "a " << m << " m o erro passou de 1 m";
    }
}

}  // namespace
