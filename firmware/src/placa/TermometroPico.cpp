// A metade da medição que fala com o periférico. A conversão vive no
// `Termometro.cpp`, portável e testada no host.
#include "placa/Termometro.h"

#include <hardware/adc.h>

namespace coruja {
namespace {

/// Canal do sensor interno no RP2350 QFN-60, que é o do Pico 2 W.
constexpr std::uint8_t kCanalSensor = 4;

bool iniciado_ = false;

}  // namespace

bool temperatura_do_chip(float* celsius) {
    if (celsius == nullptr) { return false; }
    if (!iniciado_) {
        // O ADC estava ocioso: o firmware não usa nenhum canal analógico.
        // Ligar aqui, e não no boot, mantém a inicialização junto do único
        // uso -- quem remover a medição não deixa periférico ligado para trás.
        adc_init();
        adc_set_temp_sensor_enabled(true);
        iniciado_ = true;
    }
    adc_select_input(kCanalSensor);
    *celsius = celsius_do_adc(static_cast<std::uint16_t>(adc_read()));
    return true;
}

}  // namespace coruja
