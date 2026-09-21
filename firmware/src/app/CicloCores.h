#pragma once
#include <cstddef>

#include "encoder/EventoEncoder.h"
#include "led/Calibracao.h"
#include "led/Cor.h"

namespace coruja {

/// As quatro cores de estado de via do RF03.4, na ordem de gravidade
/// crescente, percorridas pelo encoder.
///
/// A ordem não é decorativa: girar para a direita **agrava** e girar para a
/// esquerda **alivia**, então quem está na bancada consegue dizer se o sentido
/// do encoder está certo sem consultar tabela nenhuma. É a mesma ordem em que
/// o aparelho vai percorrê-las de verdade ao se aproximar de um radar.
enum class EstadoVia : std::size_t {
    Segura = 0,   ///< verde
    Ambar  = 1,   ///< aproximação conforme, e semáforo
    Rosa   = 2,   ///< faixa de margem (RF03.9)
    Perigo = 3,   ///< acima do limiar de infração
};

constexpr std::size_t kQuantosEstados = 4;

Cor         cor_do_estado(EstadoVia estado);
const char* nome_estado(EstadoVia estado);

/// Percorre as quatro cores com o encoder. Sem hardware e sem estado global:
/// é isto que permite testar a navegação inteira no host.
///
/// **Não circula.** Nas pontas ele satura, em vez de dar a volta: numa tela de
/// diagnóstico o que se quer saber é "cheguei ao fim", e um ciclo que volta
/// sozinho ao verde depois do vermelho esconde exatamente isso. O clique fica
/// livre para a atualização OTA.
class CicloCores {
public:
    EstadoVia aplica(EventoEncoder evento);

    EstadoVia estado() const { return estado_; }
    Cor       cor() const { return cor_do_estado(estado_); }
    /// `true` quando o último `aplica()` não mudou nada por já estar na ponta.
    bool      na_ponta() const { return na_ponta_; }

private:
    EstadoVia estado_ = EstadoVia::Segura;
    bool      na_ponta_ = false;
};

}  // namespace coruja
