#include "placa/PausaReal.h"

#include <pico/stdlib.h>

namespace coruja {

void PausaReal::espera_ms(std::uint32_t ms) { sleep_ms(ms); }

}  // namespace coruja
