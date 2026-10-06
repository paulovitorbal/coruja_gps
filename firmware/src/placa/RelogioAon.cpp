#include "placa/RelogioAon.h"

#include <pico/aon_timer.h>

#include <ctime>

namespace coruja {

void RelogioAon::inicia() {
    if (iniciado_) { return; }
    // Comeca em zero, que o `plausivel()` recusa -- e e exatamente o que se
    // quer dizer: ninguem acertou a hora nesta ligacao ainda.
    struct timespec zero = {};
    aon_timer_start(&zero);
    iniciado_ = true;
}

std::int64_t RelogioAon::agora_utc() const {
    if (!iniciado_) { return 0; }
    struct timespec agora = {};
    if (!aon_timer_get_time(&agora)) { return 0; }
    return static_cast<std::int64_t>(agora.tv_sec);
}

void RelogioAon::define_utc(std::int64_t segundos) {
    struct timespec t = {};
    t.tv_sec = static_cast<time_t>(segundos);
    if (!iniciado_) {
        aon_timer_start(&t);
        iniciado_ = true;
        return;
    }
    aon_timer_set_time(&t);
}

}  // namespace coruja
