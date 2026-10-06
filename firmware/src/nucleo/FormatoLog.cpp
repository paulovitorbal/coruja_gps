#include "nucleo/FormatoLog.h"

#include <cstdio>

namespace coruja {

namespace {

/// `snprintf` trunca em silêncio quando não cabe e devolve o tamanho que
/// teria escrito. Converter isso em zero é o que transforma "linha errada no
/// arquivo" em "linha ausente", que é o fracasso honesto.
std::size_t coube(int escritos, std::size_t capacidade) {
    if (escritos < 0 || static_cast<std::size_t>(escritos) >= capacidade) {
        return 0;
    }
    return static_cast<std::size_t>(escritos);
}

}  // namespace

std::size_t formata_infracao(const RegistroInfracao& r, char* destino,
                             std::size_t capacidade) {
    if (destino == nullptr || capacidade == 0) { return 0; }
    const Telemetria& m = r.momento;
    const int n = std::snprintf(
        destino, capacidade,
        "%04u-%02u-%02uT%02u:%02u:%02uZ;%.5f;%.5f;%u;%.1f;%.1f;%.1f;%u;"
        "%.5f;%.5f;%.1f\n",
        static_cast<unsigned>(m.ano), static_cast<unsigned>(m.mes),
        static_cast<unsigned>(m.dia), static_cast<unsigned>(m.hora),
        static_cast<unsigned>(m.minuto), static_cast<unsigned>(m.segundo),
        static_cast<double>(m.lat), static_cast<double>(m.lon),
        // Rumo inválido vira 999: parado o NEO-M8N deixa o campo vazio, e
        // gravar zero diria "apontando para o norte", que é afirmação falsa.
        //
        // O `% 360` fecha o círculo, e não é hipótese: a prévia no host de
        // 2026-10-05 gravou `rumo 360` numa passagem do Eixão. Arredondar
        // 359,7 dá 360,2, e o corte para inteiro deixa 360 — que não é rumo,
        // e fica a um dígito do sentinela 999.
        m.rumo_valido
            ? static_cast<unsigned>(m.rumo_graus + 0.5F) % 360U
            : 999U,
        static_cast<double>(m.velocidade_kmh),
        static_cast<double>(r.v_max_kmh),
        static_cast<double>(r.v_infra_kmh),
        static_cast<unsigned>(r.radar.limite),
        static_cast<double>(r.radar.lat), static_cast<double>(r.radar.lon),
        static_cast<double>(r.dist_min_m));
    return coube(n, capacidade);
}

std::size_t formata_ponto_viagem(const PontoViagem& p, char* destino,
                                 std::size_t capacidade) {
    if (destino == nullptr || capacidade == 0) { return 0; }
    const int n = std::snprintf(
        destino, capacidade,
        "%04u-%02u-%02uT%02u:%02u:%02uZ;%.5f;%.5f;%.1f;%.2f\n",
        static_cast<unsigned>(p.ano), static_cast<unsigned>(p.mes),
        static_cast<unsigned>(p.dia), static_cast<unsigned>(p.hora),
        static_cast<unsigned>(p.minuto), static_cast<unsigned>(p.segundo),
        static_cast<double>(p.lat), static_cast<double>(p.lon),
        static_cast<double>(p.v_media_kmh), static_cast<double>(p.dist_km));
    return coube(n, capacidade);
}

}  // namespace coruja
