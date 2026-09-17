#include "nucleo/Geo.h"

#include <cmath>

namespace coruja::geo {

namespace {
constexpr float kGrausParaRad = 0.017453292F;  // pi / 180
}

float cosseno_latitude(float lat_graus) {
    return std::cos(lat_graus * kGrausParaRad);
}

float distancia_m(float lat_a, float lon_a, float lat_b, float lon_b,
                  float cos_lat) {
    const float dy = (lat_b - lat_a) * kMetrosPorGrauLat;
    const float dx = (lon_b - lon_a) * kMetrosPorGrauLat * cos_lat;
    return std::sqrt(dx * dx + dy * dy);
}

float diferenca_rumo(float rumo_a, float rumo_b) {
    float d = std::fmod(std::fabs(rumo_a - rumo_b), 360.0F);
    return d > 180.0F ? 360.0F - d : d;
}

}  // namespace coruja::geo
