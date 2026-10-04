#pragma once
#include "nucleo/Nmea.h"
#include "nucleo/Ponto.h"
#include "nucleo/Zonamento.h"

namespace coruja {

/// Uma infração consumada: o veículo passou por um radar acima do `V_infra`
/// dele.
struct RegistroInfracao {
    /// Telemetria no **instante de menor distância** — o momento da foto.
    /// Daí saem data/hora UTC, coordenada, rumo e a velocidade aferida.
    Telemetria momento{};

    /// A maior velocidade de **toda a aproximação**, que pode ter acontecido
    /// centenas de metros antes. Junto com `momento.velocidade_kmh` mostra se
    /// o alerta fez o motorista reduzir — e quanto.
    float v_max_kmh = 0.0F;

    float v_infra_kmh = 0.0F;
    Ponto radar{};          ///< o ponto, com limite e coordenada próprias
    float dist_min_m = 0.0F;
};

/// Distância abaixo da qual se considera que o veículo **passou pelo** radar,
/// e não apenas perto dele.
///
/// Sem este corte, virar numa rua antes do radar ou seguir por via paralela
/// geraria registro de infração que não aconteceu — e num teste de estrada
/// isso é lixo que se confunde com dado. O valor é um ponto de partida; a
/// coluna `dist_min` no arquivo existe justamente para calibrá-lo depois das
/// primeiras saídas.
constexpr float kRaioPassagemM = 50.0F;

/// Detecta infração consumada a partir do fluxo de vereditos.
///
/// **Rastreia o ponto MAIS PRÓXIMO, não o alvo.** O alvo do `Veredito` vence
/// por gravidade (RF03.4): com dois radares na janela, o que está em Perigo
/// pode ser o mais distante. "Passei por este radar acima do limite dele" é
/// pergunta por ponto, e respondê-la pelo alvo atribuiria a passagem ao radar
/// errado.
///
/// **Emite no FIM da aproximação, não durante.** A 4 Hz, registrar enquanto
/// dura daria dezenas de linhas por radar. E a decisão depende de informação
/// que só existe no fim: qual foi a menor distância alcançada.
///
/// A aproximação fecha sozinha quando o ponto some do veredito — o filtro de
/// "à frente" o descarta ao ser ultrapassado, que é aproximadamente o instante
/// de maior aproximação.
class DetectorInfracao {
public:
    /// Devolve `true` e preenche `registro` quando uma aproximação fecha
    /// configurando infração. `registro` não é tocado nos outros casos.
    bool alimenta(const Veredito& v, const Telemetria& t,
                  RegistroInfracao* registro);

    void reinicia();

    bool rastreando() const { return rastreando_; }

private:
    void comeca(const Veredito& v, const Telemetria& t);
    bool fecha(RegistroInfracao* registro) const;

    bool       rastreando_ = false;
    Ponto      radar_{};
    float      v_infra_kmh_ = 0.0F;
    float      dist_min_m_ = 0.0F;
    float      v_max_kmh_ = 0.0F;
    Telemetria momento_{};
};

}  // namespace coruja
