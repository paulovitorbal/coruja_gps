#include "nucleo/AcumuladorViagem.h"

#include "nucleo/RetomadaViagem.h"

#include <cstdio>

#include "nucleo/DetectorParado.h"

namespace coruja {

const char* descreve(EstadoViagem e) {
    // ⚠️ **Rótulo de AÇÃO, não de estado** — é o que o item de menu promete.
    // `Aguardando` dizia "aguardando", que é estado, e isso passou a doer em
    // 15/10/2026: antes a espera durava os segundos entre o clique e o
    // primeiro fix, e agora dura toda parada de mais de cinco minutos. Quem
    // quisesse encerrar a gravação leria "aguardando" e não saberia que o
    // clique encerra — e o clique é a ÚNICA coisa que encerra.
    switch (e) {
        case EstadoViagem::Parada:     return "iniciar";
        case EstadoViagem::Aguardando: return "parar";
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
    // Um começo explícito nunca exige movimento: quem clicou "iniciar" quer
    // o arquivo no primeiro fix, mesmo com o carro parado.
    exige_movimento_ = false;
    ponto_ = PontoViagem{};
    nome_[0] = '\0';
}

void AcumuladorViagem::para() {
    estado_ = EstadoViagem::Parada;
    amostra_aberta_ = false;
    tem_passo_anterior_ = false;
    contando_parado_ = false;
    exige_movimento_ = false;
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
    // A zona e a contagem sao agregadas pelo PIOR da fatia, e nao pela ultima
    // leitura: a pergunta que elas respondem e "houve alerta nestes seis
    // segundos", nao "havia alerta no instante em que a fatia fechou". A
    // ordem do `Zona` e a gravidade crescente, entao o maior e o pior.
    if (r.zona_pior > radar_da_fatia_.zona_pior) {
        radar_da_fatia_.zona_pior = r.zona_pior;
    }
    if (r.n_candidatos > radar_da_fatia_.n_candidatos) {
        radar_da_fatia_.n_candidatos = r.n_candidatos;
    }
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
    ponto_.rumo_graus = ultimo_rumo_;
    ponto_.rumo_valido = ultimo_rumo_valido_;
    ponto_.radar = radar_da_fatia_;
}

void AcumuladorViagem::fecha_trecho() {
    // A fatia em curso NÃO é gravada — faltam segundos para ela fechar. É a
    // mesma perda de até 5 s do clique em "parar" e do corte de energia, e
    // pelo mesmo motivo: metade de uma média não é uma média.
    estado_ = EstadoViagem::Aguardando;
    amostra_aberta_ = false;
    tem_passo_anterior_ = false;
    contando_parado_ = false;
    exige_movimento_ = true;
    // `dist_km_` CONTINUA: quem decide se zera é o reencontro com o
    // movimento, comparando o relógio.
}

bool AcumuladorViagem::passou_da_janela(const Telemetria& t) const {
    const std::int32_t antes = minutos_utc(ponto_.ano, ponto_.mes, ponto_.dia,
                                           ponto_.hora, ponto_.minuto);
    const std::int32_t agora = minutos_utc(t.ano, t.mes, t.dia, t.hora,
                                           t.minuto);
    if (antes < 0 || agora < 0) {
        // Data que não dá para interpretar. Começar limpo é mais seguro que
        // emendar quilometragem sobre dado duvidoso.
        return true;
    }
    // Relógio andando para trás também indica dado estragado.
    if (agora < antes) { return true; }
    return static_cast<std::uint32_t>(agora - antes) > kJanelaRetomadaMin;
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
        // Depois de um fechamento por veículo parado, quem reabre é o
        // movimento. Reabrir no fix faria um arquivo a cada cinco minutos
        // com o carro estacionado.
        if (exige_movimento_) {
            if (t.velocidade_kmh < kVelocidadeParadoKmh) {
                return EventoViagem::Nada;
            }
            // Voltou a andar. A pergunta agora é se ainda é a mesma viagem,
            // e a régua é a mesma da retomada depois do corte de energia —
            // porque é a mesma pergunta.
            if (ponto_.ano != 0 && passou_da_janela(t)) {
                dist_km_ = 0.0F;
            }
            exige_movimento_ = false;
        }
        monta_nome(t);
        estado_ = EstadoViagem::Gravando;
        abre_amostra(t);
        ultima_lat_ = t.lat;
        ultima_lon_ = t.lon;
        ultimo_rumo_ = t.rumo_graus;
        ultimo_rumo_valido_ = t.rumo_valido;
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
    // O rumo acompanha a POSICAO, e nao o pior da fatia: ele descreve a
    // mesma leitura de que saem `lat` e `lon`, e so assim a perpendicular
    // calculada depois usa um par coerente.
    ultimo_rumo_ = t.rumo_graus;
    ultimo_rumo_valido_ = t.rumo_valido;

    // --- encerramento automático ---
    if (t.velocidade_kmh < kVelocidadeParadoKmh) {
        if (!contando_parado_) {
            contando_parado_ = true;
            parado_desde_ms_ = agora_ms;
        } else if (agora_ms - parado_desde_ms_ >= kParadoFechaTrechoMs) {
            fecha_trecho();
            return EventoViagem::FechaTrecho;
        }
    } else {
        contando_parado_ = false;
    }

    return evento;
}

}  // namespace coruja
