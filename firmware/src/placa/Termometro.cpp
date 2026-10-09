#include "placa/Termometro.h"

namespace coruja {
namespace {

/// Referência do ADC e largura do conversor. 12 bits, fundo de escala 3,3 V.
constexpr float kReferenciaV = 3.3F;
constexpr float kFundoDeEscala = 4095.0F;

/// Os dois números do datasheet. `kTensaoA27` é a tensão do sensor a 27 °C, e
/// `kInclinacaoVporC` quanto ela cai por grau -- a inclinação é NEGATIVA, e é
/// por isso que a fórmula subtrai.
constexpr float kTensaoA27 = 0.706F;
constexpr float kInclinacaoVporC = 0.001721F;

}  // namespace

float celsius_do_adc(std::uint16_t bruto) {
    const float tensao = static_cast<float>(bruto) * kReferenciaV / kFundoDeEscala;
    return 27.0F - (tensao - kTensaoA27) / kInclinacaoVporC;
}

}  // namespace coruja
