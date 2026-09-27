#include "buzzer/BuzzerGpio.h"

#include <hardware/gpio.h>

#include "placa/Pinos.h"

namespace coruja {

BuzzerGpio::BuzzerGpio() {
    gpio_init(pinos::kBuzzerBase);
    gpio_set_dir(pinos::kBuzzerBase, GPIO_OUT);
    gpio_put(pinos::kBuzzerBase, false);
    ligado_ = false;
}

void BuzzerGpio::define(bool ligado) {
    ligado_ = ligado;
    gpio_put(pinos::kBuzzerBase, ligado);
}

}  // namespace coruja
