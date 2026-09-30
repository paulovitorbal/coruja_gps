#include "placa/PausaReal.h"

#include <pico/stdlib.h>

namespace coruja {

void PausaReal::espera_ms(std::uint32_t ms) { sleep_ms(ms); }

std::uint32_t PausaReal::agora_ms() {
    return to_ms_since_boot(get_absolute_time());
}

}  // namespace coruja
