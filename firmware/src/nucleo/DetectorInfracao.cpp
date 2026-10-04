#include "nucleo/DetectorInfracao.h"

#include "nucleo/LimiarInfracao.h"

namespace coruja {

namespace {

/// Mesmo radar?
///
/// Comparação **exata** de float, e de propósito. Os dois valores são cópias
/// do mesmo elemento da base — o `Veredito` reentrega `base[i]` por valor a
/// cada ciclo —, então ou são o mesmo ponto e os bits são idênticos, ou são
/// pontos diferentes. Tolerância aqui não corrigiria nada e fundiria dois
/// radares vizinhos num só.
bool mesmo_radar(const Ponto& a, const Ponto& b) {
    return a.lat == b.lat && a.lon == b.lon;
}

}  // namespace

void DetectorInfracao::comeca(const Veredito& v, const Telemetria& t) {
    rastreando_ = true;
    radar_ = v.mais_proximo;
    v_infra_kmh_ = velocidade_infracao(v.mais_proximo.limite);
    dist_min_m_ = v.dist_mais_proximo_m;
    v_max_kmh_ = t.velocidade_kmh;
    momento_ = t;
}

bool DetectorInfracao::fecha(RegistroInfracao* registro) const {
    if (dist_min_m_ > kRaioPassagemM) {
        return false;  // passou perto, não passou por ele
    }
    if (momento_.velocidade_kmh <= v_infra_kmh_) {
        return false;  // passou dentro do limite
    }
    if (registro != nullptr) {
        registro->momento = momento_;
        registro->v_max_kmh = v_max_kmh_;
        registro->v_infra_kmh = v_infra_kmh_;
        registro->radar = radar_;
        registro->dist_min_m = dist_min_m_;
    }
    return true;
}

bool DetectorInfracao::alimenta(const Veredito& v, const Telemetria& t,
                                RegistroInfracao* registro) {
    // Semáforo sem limite não afere velocidade (RF03.3): `velocidade_infracao`
    // devolve zero nele, e qualquer velocidade superaria zero.
    const bool rastreavel =
        v.tem_mais_proximo && v.mais_proximo.limite != kSemLimite;

    bool emitiu = false;

    // **Fecha antes de abrir**, e as duas coisas podem acontecer na mesma
    // chamada: com dois radares em sequência, o mais próximo troca de um para
    // o outro sem passar por nenhum ciclo vazio.
    if (rastreando_ && (!rastreavel || !mesmo_radar(v.mais_proximo, radar_))) {
        emitiu = fecha(registro);
        rastreando_ = false;
    }

    if (rastreavel) {
        if (!rastreando_) {
            comeca(v, t);
        } else {
            if (t.velocidade_kmh > v_max_kmh_) {
                v_max_kmh_ = t.velocidade_kmh;
            }
            if (v.dist_mais_proximo_m < dist_min_m_) {
                dist_min_m_ = v.dist_mais_proximo_m;
                momento_ = t;
            }
        }
    }

    return emitiu;
}

void DetectorInfracao::reinicia() {
    rastreando_ = false;
    radar_ = Ponto{};
    v_infra_kmh_ = 0.0F;
    dist_min_m_ = 0.0F;
    v_max_kmh_ = 0.0F;
    momento_ = Telemetria{};
}

}  // namespace coruja
