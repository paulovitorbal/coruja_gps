#include "nucleo/AcumuladorViagem.h"

#include <cstdio>

#include "nucleo/DetectorParado.h"

namespace coruja {

const char* descreve(EstadoViagem e) {
    switch (e) {
        case EstadoViagem::Parada:     return "iniciar";
        case EstadoViagem::Aguardando: return "aguardando";
        case EstadoViagem::Gravando:   return "parar";
    }
    return "?";
}

void AcumuladorViagem::inicia(float dist_inicial_km) {
    estado_ = EstadoViagem::Aguardando;
    dist_km_ = dist_inicial_km;
    tem_passo_anterior_ = false;
    amostra_aberta_ = false;
    contando_parado_ = false;
    nome_[0] = '\0';
}

void AcumuladorViagem::para() {
    estado_ = EstadoViagem::Parada;
    amostra_aberta_ = false;
    tem_passo_anterior_ = false;
    contando_parado_ = false;
}

void AcumuladorViagem::monta_nome(const Telemetria& t) {
    std::snprintf(nome_, sizeof nome_, "%04u%02u%02u_%02u%02u%02u.log",
                  static_cast<unsigned>(t.ano), static_cast<unsigned>(t.mes),
                  static_cast<unsigned>(t.dia), static_cast<unsigned>(t.hora),
                  static_cast<unsigned>(t.minuto),
                  static_cast<unsigned>(t.segundo));
}

std::uint8_t AcumuladorViagem::fatia_de(const Telemetria& t) {
    return static_cast<std::uint8_t>(t.segundo / kSegundosPorAmostra);
}

void AcumuladorViagem::abre_amostra(const Telemetria& t) {
    amostra_aberta_ = true;
    ano_ = t.ano; mes_ = t.mes; dia_ = t.dia;
    hora_ = t.hora; minuto_ = t.minuto;
    fatia_ = fatia_de(t);
    soma_vel_ = 0.0F;
    amostras_ = 0;
    radar_da_fatia_ = RadarDaAmostra{};
}

void AcumuladorViagem::considera_radar(const RadarDaAmostra& r) {
    // Os dois lados sao independentes: o alerta pode existir numa leitura em
    // que nao ha mais proximo registrado, e vice-versa.
    if (r.tem_alerta && (!radar_da_fatia_.tem_alerta ||
                         r.dist_alerta_m < radar_da_fatia_.dist_alerta_m)) {
        radar_da_fatia_.tem_alerta = true;
        radar_da_fatia_.dist_alerta_m = r.dist_alerta_m;
        radar_da_fatia_.limite_alerta = r.limite_alerta;
    }
    if (r.tem_proximo && (!radar_da_fatia_.tem_proximo ||
                          r.dist_proximo_m < radar_da_fatia_.dist_proximo_m)) {
        radar_da_fatia_.tem_proximo = true;
        radar_da_fatia_.dist_proximo_m = r.dist_proximo_m;
        radar_da_fatia_.limite_proximo = r.limite_proximo;
    }
}

void AcumuladorViagem::fecha_amostra() {
    ponto_.ano = ano_; ponto_.mes = mes_; ponto_.dia = dia_;
    ponto_.hora = hora_; ponto_.minuto = minuto_;
    // O segundo e o INICIO da fatia, nao o instante da ultima amostra: a
    // linha descreve a fatia inteira, e um carimbo em `:07` sugeriria um
    // instante medido.
    ponto_.segundo = static_cast<std::uint8_t>(fatia_ * kSegundosPorAmostra);
    ponto_.lat = ultima_lat_;
    ponto_.lon = ultima_lon_;
    ponto_.v_media_kmh =
        amostras_ > 0 ? soma_vel_ / static_cast<float>(amostras_) : 0.0F;
    ponto_.dist_km = dist_km_;
    ponto_.radar = radar_da_fatia_;
}

EventoViagem AcumuladorViagem::alimenta(const Telemetria& t,
                                        const RadarDaAmostra& radar,
                                        bool tem_fix,
                                        std::uint32_t agora_ms) {
    if (estado_ == EstadoViagem::Parada) {
        return EventoViagem::Nada;
    }

    if (!tem_fix || !t.data_valida) {
        // **Não se zera o passo de integração aqui.** A primeira versão
        // zerava, e a mutação mostrou que era código morto: o teto de
        // `kMaxPassoIntegracaoMs` já corta qualquer buraco longo. Pior, zerar
        // descartava buracos CURTOS que valia a pena integrar — um segundo
        // sem sentença, com velocidade conhecida dos dois lados, estima-se
        // bem melhor do que se despreza.
        //
        // Sem fix não há prova de estar parado: contar o túnel como parada
        // encerraria a viagem no meio de uma estrada.
        contando_parado_ = false;
        return EventoViagem::Nada;
    }

    if (estado_ == EstadoViagem::Aguardando) {
        monta_nome(t);
        estado_ = EstadoViagem::Gravando;
        abre_amostra(t);
        ultima_lat_ = t.lat;
        ultima_lon_ = t.lon;
        soma_vel_ += t.velocidade_kmh;
        considera_radar(radar);
        ++amostras_;
        anterior_ms_ = agora_ms;
        tem_passo_anterior_ = true;
        return EventoViagem::Abre;
    }

    // --- integração da distância ---
    if (tem_passo_anterior_) {
        const std::uint32_t passo_ms = agora_ms - anterior_ms_;
        if (passo_ms <= kMaxPassoIntegracaoMs) {
            dist_km_ += t.velocidade_kmh *
                        (static_cast<float>(passo_ms) / 3600000.0F);
        }
    }
    anterior_ms_ = agora_ms;
    tem_passo_anterior_ = true;

    // --- virada da fatia ---
    //
    // Compara minuto E fatia: so a fatia daria falso negativo na virada do
    // minuto, quando ela volta de 9 para 0 sem passar por valor diferente
    // de algum ja visto.
    EventoViagem evento = EventoViagem::Nada;
    const std::uint8_t fatia_agora = fatia_de(t);
    if (amostra_aberta_ && (t.minuto != minuto_ || fatia_agora != fatia_)) {
        fecha_amostra();
        abre_amostra(t);
        evento = EventoViagem::Grava;
    } else if (!amostra_aberta_) {
        abre_amostra(t);
    }

    // DEPOIS da virada, de proposito: a leitura de agora pertence a fatia que
    // a contem, e nao a que acabou de ser fechada.
    soma_vel_ += t.velocidade_kmh;
    considera_radar(radar);
    ++amostras_;
    ultima_lat_ = t.lat;
    ultima_lon_ = t.lon;

    // --- encerramento automático ---
    if (t.velocidade_kmh < kVelocidadeParadoKmh) {
        if (!contando_parado_) {
            contando_parado_ = true;
            parado_desde_ms_ = agora_ms;
        } else if (agora_ms - parado_desde_ms_ >= kParadoEncerraViagemMs) {
            para();
            return EventoViagem::Encerra;
        }
    } else {
        contando_parado_ = false;
    }

    return evento;
}

}  // namespace coruja
