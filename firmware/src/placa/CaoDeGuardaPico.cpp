#include "placa/CaoDeGuardaPico.h"

#include <hardware/watchdog.h>

namespace coruja {

void CaoDeGuardaPico::arma(std::uint32_t prazo_ms) {
    prazo_ms_ = prazo_ms;
    // `false`: o cão NÃO pausa com o depurador parado. Pausar seria cômodo na
    // bancada e mentiroso em campo — o firmware do carro roda sem depurador, e
    // a configuração que se testa tem de ser a que se usa.
    watchdog_enable(prazo_ms, false);
}

void CaoDeGuardaPico::alimenta() { watchdog_update(); }

void CaoDeGuardaPico::suspende() { watchdog_disable(); }

void CaoDeGuardaPico::retoma() { watchdog_enable(prazo_ms_, false); }

bool CaoDeGuardaPico::reiniciou_por_travamento() const {
    // `watchdog_enable_caused_reboot`, e não `watchdog_caused_reboot`.
    //
    // O segundo diz "sim" para QUALQUER reinício pelo bloco do watchdog,
    // inclusive o que o `picotool` provoca e o que arrastar um `.uf2` para o
    // RPI-RP2 provoca. Numa sessão de gravação isso sairia no log como
    // travamento, e um aviso que mente é pior que a falta dele.
    //
    // O primeiro confere ainda a marca que `watchdog_enable` deixa no
    // rascunho 4 — ou seja, responde à pergunta que de fato interessa: foi o
    // cão QUE NÓS armamos que mordeu?
    return watchdog_enable_caused_reboot();
}

}  // namespace coruja
