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

}  // namespace

void MaquinaZona::Candidato::considera(std::size_t i, float d) {
    if (!tem || d < distancia) {
        tem = true;
        indice = i;
        distancia = d;
    }
}

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

bool ponto_a_frente(const RumoFiltro& rumo, const Telemetria& t,
                    const Ponto& p, float cos_lat) {
    if (!rumo.utilizavel) { return true; }

    const float norte = (p.lat - t.lat) * geo::kMetrosPorGrauLat;
    const float leste = (p.lon - t.lon) * geo::kMetrosPorGrauLat * cos_lat;
    // Em cima do ponto o azimute é indefinido; `atan2(0,0)` devolveria zero e
    // descartaria o alvo por um ângulo que não existe.
    if (norte == 0.0F && leste == 0.0F) { return true; }

    const float azimute =
        normaliza_graus(std::atan2(leste, norte) * kGrausPorRadiano);
    return geo::diferenca_rumo(rumo.graus, azimute) <= kAnguloAFrenteGraus;
}

bool sentido_compativel(const RumoFiltro& rumo, const Ponto& p) {
    if (p.sentido == Sentido::Omnidirecional) { return true; }
    if (!rumo.utilizavel) { return true; }

    float diferenca =
        geo::diferenca_rumo(rumo.graus, static_cast<float>(p.rumo_graus()));
    if (p.sentido == Sentido::Bidirecional) {
        const float oposto = 180.0F - diferenca;
        diferenca = (oposto < diferenca) ? oposto : diferenca;
    }
    return diferenca <= kToleranciaRumoGraus;
}

bool decompoe_lateral(const RumoFiltro& rumo, const Telemetria& t,
                      const Ponto& p, float cos_lat, float* ao_longo_m,
                      float* lateral_m) {
    if (!rumo.utilizavel) { return false; }

    const float norte = (p.lat - t.lat) * geo::kMetrosPorGrauLat;
    const float leste = (p.lon - t.lon) * geo::kMetrosPorGrauLat * cos_lat;

    // Versor de avanço é (sen θ, cos θ) porque o rumo é medido do NORTE no
    // sentido horário — não do leste no anti-horário, como a trigonometria
    // de escola. Trocar os dois espelharia o mapa na diagonal.
    const float rad = rumo.graus / kGrausPorRadiano;
    const float sen = std::sin(rad);
    const float co  = std::cos(rad);

    if (ao_longo_m != nullptr) { *ao_longo_m = leste * sen + norte * co; }
    if (lateral_m != nullptr)  { *lateral_m  = leste * co - norte * sen; }
    return true;
}

bool ponto_na_minha_via(const RumoFiltro& rumo, const Telemetria& t,
                        const Ponto& p, float cos_lat, bool proa_estavel) {
    if (!proa_estavel) { return true; }

    float ao_longo = 0.0F;
    float lateral = 0.0F;
    if (!decompoe_lateral(rumo, t, p, cos_lat, &ao_longo, &lateral)) {
        return true;
    }

    // Para trás quem responde é `ponto_a_frente`. Aqui o que importa é que a
    // folga do limiar é proporcional à distância À FRENTE, e distância
    // negativa a encolheria — um ponto às costas ganharia um veto severo que
    // ninguém pediu.
    if (ao_longo <= 0.0F) { return true; }

    const float limiar = kDesvioLateralM + ao_longo * kFolgaLateralPorMetro;
    const float desvio = (lateral < 0.0F) ? -lateral : lateral;
    return desvio <= limiar;
}

void ProaRecente::anota(float graus) {
    proas_[proxima_] = graus;
    proxima_ = (proxima_ + 1) % kAmostrasDeProa;
    if (quantas_ < kAmostrasDeProa) { ++quantas_; }
}

void ProaRecente::esquece() {
    quantas_ = 0;
    proxima_ = 0;
}

bool ProaRecente::estavel() const {
    // Janela incompleta é ausência de informação, não ausência de giro.
    if (quantas_ < kAmostrasDeProa) { return false; }

    // Com o anel cheio, `proxima_` é justamente a posição mais antiga: é a
    // que será sobrescrita a seguir.
    const std::size_t ultima =
        (proxima_ + kAmostrasDeProa - 1) % kAmostrasDeProa;
    return geo::diferenca_rumo(proas_[proxima_], proas_[ultima]) <=
           kGiroMaximoGraus;
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
    tem_rumo_lembrado_ = false;
    proa_.esquece();
    cruzeiro_.esquece();
}

/// Qual rumo os filtros usam, e a manutenção da memória.
///
/// ## Por que lembrar, em vez de abrir o filtro
///
/// Antes, sem rumo utilizável os dois filtros abriam e **todo ponto a menos de
/// 300 m virava candidato — inclusive os que ficaram para trás**. O sintoma
/// apareceu dirigindo, em 2026-10-06: passar por um radar, parar num semáforo
/// 200 m adiante, e ver na tela o radar já ultrapassado.
///
/// **Carro parado não gira.** O rumo de instantes antes continua sendo a
/// direção para onde ele aponta, e usá-lo é mais verdadeiro que fingir que não
/// se sabe nada.
///
/// ## A trava que mantém a falha segura
///
/// A memória só vale enquanto o veículo **não se deslocou** desde a leitura
/// boa. Se ele andou sem produzir rumo novo — fila a passo, manobra —, pode ter
/// mudado de direção, e aí o filtro volta a abrir como antes.
///
/// Isso preserva a assimetria que importa: esconder um radar à frente é
/// perigoso, mostrar um que ficou para trás é só irritante. Na dúvida, abre.
RumoFiltro MaquinaZona::rumo_para_filtro(const Telemetria& t, float cos_lat) {
    if (rumo_utilizavel(t)) {
        tem_rumo_lembrado_ = true;
        rumo_lembrado_ = t.rumo_graus;
        lat_rumo_ = t.lat;
        lon_rumo_ = t.lon;
        // Só leitura de verdade entra na janela do giro. O rumo LEMBRADO
        // repetiria o mesmo valor a cada volta e faria qualquer parada
        // parecer uma reta perfeita.
        proa_.anota(t.rumo_graus);
        return {true, t.rumo_graus};
    }
    if (!tem_rumo_lembrado_) {
        proa_.esquece();
        return {};
    }

    const float andou =
        geo::distancia_m(lat_rumo_, lon_rumo_, t.lat, t.lon, cos_lat);
    if (andou > kRaioRumoCongeladoM) {
        tem_rumo_lembrado_ = false;
        proa_.esquece();
        return {};
    }
    return {true, rumo_lembrado_};
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
    // Alertas suspensos de imediato. O LED vai a AZUL FIXO, e não apagado:
    // ver a justificativa em `PadraoLed::cor` — apagado significaria duas
    // coisas ao mesmo tempo, "perdi o GPS" e "o aparelho morreu".
    v.zona = Zona::SemSinal;
    return v;
}

MaquinaZona::Disputa MaquinaZona::varre(const Telemetria& t,
                                        const Ponto* base, std::size_t n,
                                        float cos_lat, const RumoFiltro& rumo,
                                        bool proa_estavel,
                                        std::uint8_t limite_minimo) const {
    Disputa d;

    // O teto do que o cruzeiro explica. Zero com cruzeiro inválido, e aí
    // `limite_da_via` fica zero e o desempate não acontece.
    const float teto = cruzeiro_.valida()
                           ? cruzeiro_.kmh() * kFolgaDoLimite
                           : 0.0F;

    // A varredura usa o MAIOR raio de saída, e não o do ponto: a janela de
    // busca precisa alcançar o alvo retido pela histerese mais folgada que
    // existe, senão um ponto de via rápida some da busca antes de ser
    // avaliado. O raio de cada ponto é aplicado lá dentro.
    const float margem_lat = kRaioSaidaM / geo::kMetrosPorGrauLat;
    const float margem_lon = kRaioSaidaM / (geo::kMetrosPorGrauLat * cos_lat);
    const float lat_maxima = t.lat + margem_lat;

    for (std::size_t i = primeiro_com_lat_ge(base, n, t.lat - margem_lat);
         i < n && base[i].lat <= lat_maxima; ++i) {
        const Ponto& p = base[i];

        // Corte por longitude antes da distância: comparação de floats contra
        // um limiar calculado uma vez por fix, sem trigonometria por ponto.
        const float dlon = p.lon - t.lon;
        if (dlon > margem_lon || dlon < -margem_lon) { continue; }

        const float dist = geo::distancia_m(t.lat, t.lon, p.lat, p.lon, cos_lat);

        // Histerese de distância (RF03.2): entra no raio do ponto, e só o
        // alvo já vigente continua valendo até o raio de saída dele.
        const bool e_o_alvo = tem_alvo_ && i == indice_alvo_;
        const float raio =
            e_o_alvo ? raio_de_saida(p.limite) : raio_de_alerta(p.limite);
        if (dist > raio) { continue; }

        if (!sentido_compativel(rumo, p)) { continue; }
        if (!ponto_a_frente(rumo, t, p, cos_lat)) { continue; }
        if (!ponto_na_minha_via(rumo, t, p, cos_lat, proa_estavel)) {
            continue;
        }

        // O corte do desempate (RF03.11), e ele **poupa o semáforo**: limite
        // zero não descreve via nenhuma, e suprimi-lo esconderia câmera de
        // avanço de sinal por causa de uma disputa que não é dele.
        if (limite_minimo != kSemLimite && p.limite != kSemLimite &&
            p.limite < limite_minimo) {
            continue;
        }

        if (p.limite != kSemLimite) {
            if (static_cast<float>(p.limite) <= teto &&
                p.limite > d.limite_da_via) {
                d.limite_da_via = p.limite;
            }
            if (d.menor_limite == kSemLimite || p.limite < d.menor_limite) {
                d.menor_limite = p.limite;
            }
        }

        // `e_perigo` vem primeiro de propósito: a guarda de `kSemLimite`
        // dentro dele é o **único** ponto que impede um semáforo de virar
        // Perigo. Testar `limite == 0` aqui antes duplicaria a regra e
        // deixaria a guarda como código morto, que ninguém percebe quebrar.
        d.mais_proximo.considera(i, dist);
        if (d.n_candidatos < 255) { ++d.n_candidatos; }

        if (e_perigo(t.velocidade_kmh, p.limite)) {
            d.perigo.considera(i, dist);
        } else if (p.limite == kSemLimite) {
            d.semaforo.considera(i, dist);
        } else if (t.velocidade_kmh > static_cast<float>(p.limite)) {
            d.margem.considera(i, dist);
        } else {
            d.conforme.considera(i, dist);
        }
    }
    return d;
}

Veredito MaquinaZona::avalia(const Telemetria& t, const Ponto* base,
                             std::size_t n, std::uint32_t agora_ms) {
    ultimo_fix_ms_ = agora_ms;
    houve_fix_ = true;
    cruzeiro_.anota(t.velocidade_kmh, agora_ms);

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

    const RumoFiltro rumo = rumo_para_filtro(t, cos_lat);

    // Lido DEPOIS de `rumo_para_filtro`, que é quem alimenta a janela: a
    // amostra de agora tem de contar para o giro de agora.
    const bool proa_estavel = proa_.estavel();

    Disputa d = varre(t, base, n, cos_lat, rumo, proa_estavel, kSemLimite);

    // ---------------------------------------------- desempate por cruzeiro
    //
    // 🔴 **O caso que a geometria não resolve.** Medido em 28/10/2026, numa
    // via rápida com marginal: o radar de 60 da lateral fica a 6,6 m contra
    // 3,4 m do radar de 80 da pista principal: separação da ordem do ruído do
    // GPS, e o filtro de pista (RF02.4) não a encontra. A evidência que
    // sobra é a velocidade sustentada — quem vem há minutos a 78 km/h está na
    // via de 80.
    //
    // **Só desempata disputa.** Com um candidato só, ou com todos do mesmo
    // limite, nada acontece: `menor_limite` não fica abaixo de
    // `limite_da_via` e a segunda varredura nem roda. É isso que impede o
    // desempate de calar um radar solitário, que seria trocar um incômodo por
    // uma multa.
    const std::uint8_t candidatos_em_disputa = d.n_candidatos;
    if (d.limite_da_via != kSemLimite && d.menor_limite != kSemLimite &&
        d.menor_limite < d.limite_da_via) {
        d = varre(t, base, n, cos_lat, rumo, proa_estavel, d.limite_da_via);
    }

    const Candidato& perigo = d.perigo;
    const Candidato& margem = d.margem;
    const Candidato& semaforo = d.semaforo;
    const Candidato& conforme = d.conforme;
    const Candidato& mais_proximo = d.mais_proximo;
    // A contagem da PRIMEIRA varredura: o log tem de dizer que houve disputa,
    // e não o tamanho do que sobrou depois de arbitrá-la.
    const std::uint8_t n_candidatos = candidatos_em_disputa;

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
    v.n_candidatos = n_candidatos;
    // Preenchido ANTES do retorno antecipado seria codigo morto: sem
    // candidato escolhido tambem nao ha mais proximo, porque os dois saem do
    // mesmo laco e dos mesmos filtros.
    if (mais_proximo.tem) {
        v.tem_mais_proximo = true;
        v.mais_proximo = base[mais_proximo.indice];
        v.dist_mais_proximo_m = mais_proximo.distancia;
    }
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
