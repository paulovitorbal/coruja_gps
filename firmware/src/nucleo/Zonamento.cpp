#include "nucleo/Zonamento.h"

#include <cmath>

#include "nucleo/Geo.h"
#include "nucleo/LimiarInfracao.h"

namespace coruja {
namespace {

constexpr float kPi = 3.14159265F;
constexpr float kGrausPorRadiano = 180.0F / kPi;

/// Menor cosseno aceito antes de a conversão de longitude estourar. Em
/// território brasileiro `cos(lat) > 0.95`; a guarda cobre latitude absurda
/// vinda de um fix corrompido, não o uso normal.
constexpr float kCosLatMinimo = 0.01F;

/// Multiplicadores das faixas sonoras do RF03.7, ancorados em `V_infra`.
constexpr float kFatorFaixaRapida = 1.10F;
constexpr float kFatorFaixaPulso  = 1.20F;

float normaliza_graus(float graus) {
    float g = std::fmod(graus, 360.0F);
    if (g < 0.0F) { g += 360.0F; }
    return g;
}

/// O rumo do veículo é utilizável neste instante?
///
/// Parado, o NEO-M8N publica um azimute que é ruído: o receptor deriva o rumo
/// do vetor velocidade, e com velocidade nula o vetor é só erro. Filtrar por
/// esse valor descartaria pontos reais, então tanto o RF02.3 quanto o RF03.1
/// **abrem** o filtro abaixo do piso.
bool rumo_utilizavel(const Telemetria& t) {
    return t.rumo_valido && t.velocidade_kmh >= kVelocidadeMinimaRumoKmh;
}

/// O mais próximo de uma categoria do RF03.4.
struct Candidato {
    bool        tem = false;
    std::size_t indice = 0;
    float       distancia = 0.0F;

    void considera(std::size_t i, float d) {
        if (!tem || d < distancia) {
            tem = true;
            indice = i;
            distancia = d;
        }
    }
};

}  // namespace

const char* descreve(Zona zona) {
    switch (zona) {
        case Zona::SemSinal:            return "sem sinal";
        case Zona::Segura:              return "segura";
        case Zona::AproximacaoConforme: return "aproximacao/conforme";
        case Zona::Semaforo:            return "semaforo";
        case Zona::AproximacaoMargem:   return "aproximacao/margem";
        case Zona::Perigo:              return "perigo";
    }
    return "?";
}

const char* descreve(FaixaSonora faixa) {
    switch (faixa) {
        case FaixaSonora::Nenhuma: return "silencio";
        case FaixaSonora::Lenta:   return "lenta";
        case FaixaSonora::Rapida:  return "rapida";
        case FaixaSonora::Pulso:   return "pulso";
    }
    return "?";
}

std::size_t primeiro_com_lat_ge(const Ponto* base, std::size_t n, float lat) {
    std::size_t baixo = 0;
    std::size_t alto = n;
    while (baixo < alto) {
        const std::size_t meio = baixo + (alto - baixo) / 2;
        if (base[meio].lat < lat) {
            baixo = meio + 1;
        } else {
            alto = meio;
        }
    }
    return baixo;
}

bool ponto_a_frente(const Telemetria& t, const Ponto& p, float cos_lat) {
    if (!rumo_utilizavel(t)) { return true; }

    const float norte = (p.lat - t.lat) * geo::kMetrosPorGrauLat;
    const float leste = (p.lon - t.lon) * geo::kMetrosPorGrauLat * cos_lat;
    // Em cima do ponto o azimute é indefinido; `atan2(0,0)` devolveria zero e
    // descartaria o alvo por um ângulo que não existe.
    if (norte == 0.0F && leste == 0.0F) { return true; }

    const float azimute =
        normaliza_graus(std::atan2(leste, norte) * kGrausPorRadiano);
    return geo::diferenca_rumo(t.rumo_graus, azimute) <= kAnguloAFrenteGraus;
}

bool sentido_compativel(const Telemetria& t, const Ponto& p) {
    if (p.sentido == Sentido::Omnidirecional) { return true; }
    if (!rumo_utilizavel(t)) { return true; }

    float diferenca =
        geo::diferenca_rumo(t.rumo_graus, static_cast<float>(p.rumo_graus()));
    if (p.sentido == Sentido::Bidirecional) {
        const float oposto = 180.0F - diferenca;
        diferenca = (oposto < diferenca) ? oposto : diferenca;
    }
    return diferenca <= kToleranciaRumoGraus;
}

bool MaquinaZona::e_perigo(float velocidade_kmh, std::uint8_t limite) const {
    // Sem limite não há infração a detectar: `velocidade_infracao` devolve
    // zero, e comparar contra zero poria todo semáforo em Perigo.
    if (limite == kSemLimite) { return false; }
    const float v_infra = velocidade_infracao(limite);
    const float limiar = (zona_ == Zona::Perigo) ? v_infra - kHistereseVelKmh
                                                 : v_infra;
    return velocidade_kmh > limiar;
}

void MaquinaZona::reinicia() {
    zona_ = Zona::Segura;
    faixa_ = FaixaSonora::Nenhuma;
    tem_alvo_ = false;
    indice_alvo_ = 0;
    houve_fix_ = false;
}

Veredito MaquinaZona::sem_fix(std::uint32_t agora_ms) {
    // O alvo sobrevive por 10 s (§4.1): um viaduto não deve custar o alerta.
    // Passado esse prazo a posição está velha demais para se alertar sobre
    // ela, e aí a histerese também é zerada — ao voltar o sinal o veículo
    // pode estar em outro lugar, e continuidade seria uma mentira.
    if (tem_alvo_ && houve_fix_ &&
        (agora_ms - ultimo_fix_ms_) >= kDescarteAlvoMs) {
        tem_alvo_ = false;
        zona_ = Zona::Segura;
        faixa_ = FaixaSonora::Nenhuma;
    }
    Veredito v;
    v.zona = Zona::SemSinal;  // alertas suspensos de imediato, LED apagado
    return v;
}

Veredito MaquinaZona::avalia(const Telemetria& t, const Ponto* base,
                             std::size_t n, std::uint32_t agora_ms) {
    ultimo_fix_ms_ = agora_ms;
    houve_fix_ = true;

    // Base trocada sob os pés: o índice sozinho não identifica o alvo, porque
    // uma recarga pode pôr outro ponto na mesma posição do array. Coordenadas
    // confirmam a identidade. O caminho certo continua sendo `reinicia()`;
    // isto é a rede embaixo dele.
    if (tem_alvo_ && (indice_alvo_ >= n || base[indice_alvo_].lat != lat_alvo_ ||
                      base[indice_alvo_].lon != lon_alvo_)) {
        tem_alvo_ = false;
    }

    float cos_lat = geo::cosseno_latitude(t.lat);
    if (cos_lat < kCosLatMinimo) { cos_lat = kCosLatMinimo; }

    // A varredura usa o raio de **saída**, não o de entrada: o alvo retido
    // pela histerese vive entre 300 e 340 m, e some da busca se a janela for
    // estreita demais.
    const float margem_lat = kRaioSaidaM / geo::kMetrosPorGrauLat;
    const float margem_lon = kRaioSaidaM / (geo::kMetrosPorGrauLat * cos_lat);
    const float lat_maxima = t.lat + margem_lat;

    Candidato perigo;
    Candidato margem;
    Candidato semaforo;
    Candidato conforme;

    for (std::size_t i = primeiro_com_lat_ge(base, n, t.lat - margem_lat);
         i < n && base[i].lat <= lat_maxima; ++i) {
        const Ponto& p = base[i];

        // Corte por longitude antes da distância: comparação de floats contra
        // um limiar calculado uma vez por fix, sem trigonometria por ponto.
        const float dlon = p.lon - t.lon;
        if (dlon > margem_lon || dlon < -margem_lon) { continue; }

        const float d = geo::distancia_m(t.lat, t.lon, p.lat, p.lon, cos_lat);

        // Histerese de distância (RF03.2): entra a 300 m, e só o alvo já
        // vigente continua valendo até 340 m.
        const bool e_o_alvo = tem_alvo_ && i == indice_alvo_;
        const float raio = e_o_alvo ? kRaioSaidaM : kRaioAlertaM;
        if (d > raio) { continue; }

        if (!sentido_compativel(t, p)) { continue; }
        if (!ponto_a_frente(t, p, cos_lat)) { continue; }

        // `e_perigo` vem primeiro de propósito: a guarda de `kSemLimite`
        // dentro dele é o **único** ponto que impede um semáforo de virar
        // Perigo. Testar `limite == 0` aqui antes duplicaria a regra e
        // deixaria a guarda como código morto, que ninguém percebe quebrar.
        if (e_perigo(t.velocidade_kmh, p.limite)) {
            perigo.considera(i, d);
        } else if (p.limite == kSemLimite) {
            semaforo.considera(i, d);
        } else if (t.velocidade_kmh > static_cast<float>(p.limite)) {
            margem.considera(i, d);
        } else {
            conforme.considera(i, d);
        }
    }

    // Precedência do RF03.4: vence a categoria mais grave, não o ponto mais
    // próximo. Dois radares na janela com limites diferentes produzem
    // candidatos distintos ao mesmo tempo, e mostrar o mais próximo
    // esconderia a multa em curso no outro.
    const Candidato* escolhido = nullptr;
    Zona zona = Zona::Segura;
    if (perigo.tem) {
        escolhido = &perigo;
        zona = Zona::Perigo;
    } else if (margem.tem) {
        escolhido = &margem;
        zona = Zona::AproximacaoMargem;
    } else if (semaforo.tem) {
        escolhido = &semaforo;
        zona = Zona::Semaforo;
    } else if (conforme.tem) {
        escolhido = &conforme;
        zona = Zona::AproximacaoConforme;
    }

    Veredito v;
    v.zona = zona;
    if (escolhido == nullptr) {
        tem_alvo_ = false;
        zona_ = Zona::Segura;
        faixa_ = FaixaSonora::Nenhuma;
        return v;
    }

    tem_alvo_ = true;
    indice_alvo_ = escolhido->indice;
    lat_alvo_ = base[escolhido->indice].lat;
    lon_alvo_ = base[escolhido->indice].lon;
    v.tem_alvo = true;
    v.alvo = base[escolhido->indice];
    v.distancia_m = escolhido->distancia;
    v.v_infra_kmh = velocidade_infracao(v.alvo.limite);

    // Só a Zona de Perigo soa (RF03.8). A faixa é recalculada aqui, depois de
    // o alvo estar escolhido, porque o `V_infra` é o dele.
    zona_ = zona;
    faixa_ = (zona == Zona::Perigo)
                 ? faixa_sonora(t.velocidade_kmh, v.v_infra_kmh)
                 : FaixaSonora::Nenhuma;
    v.faixa = faixa_;
    return v;
}

FaixaSonora MaquinaZona::faixa_sonora(float velocidade_kmh, float v_infra) {
    // Sobe ao cruzar o limiar; desce só 2 km/h abaixo dele (RF03.7). Sem
    // isso, velocidade oscilando sobre uma fronteira faz o padrão tremular.
    const auto acima = [&](float limiar, FaixaSonora desta) {
        const float efetivo =
            (faixa_ >= desta) ? limiar - kHistereseVelKmh : limiar;
        return velocidade_kmh > efetivo;
    };

    if (acima(v_infra * kFatorFaixaPulso, FaixaSonora::Pulso)) {
        return FaixaSonora::Pulso;
    }
    if (acima(v_infra * kFatorFaixaRapida, FaixaSonora::Rapida)) {
        return FaixaSonora::Rapida;
    }
    if (acima(v_infra, FaixaSonora::Lenta)) {
        return FaixaSonora::Lenta;
    }
    return FaixaSonora::Nenhuma;
}

}  // namespace coruja
