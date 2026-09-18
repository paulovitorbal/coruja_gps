#pragma once
#include <cstddef>

/// Mapa único de pinos do Coruja GPS.
///
/// **Esta é a fonte da verdade do firmware.** Antes deste arquivo os pinos
/// viviam espalhados: três no `LedRgbAnodoComum`, três no `EncoderKy040`, e os
/// outros doze só em prosa no `bom_schematic.md`. Um mapa espalhado é um mapa
/// que divergiu — a pergunta "o GPIO 15 já está usado?" não tinha onde ser
/// respondida.
///
/// Transcrito do `bom_schematic.md` e conferido contra a netlist do
/// `gera_fritzing.py`, que é quem desenha o esquema.
///
/// O **número físico** de cada pino vem no comentário porque os dois sistemas
/// de numeração coexistem no projeto: o firmware fala em GPIO, o documento de
/// montagem e a serigrafia falam em pino físico, e a tradução errada é fonte
/// de fiação errada.
namespace coruja::pinos {

// ---------------------------------------------------------------------------
// GPS — UART0
// ---------------------------------------------------------------------------

/// Pico transmite, GPS recebe. Vai por um resistor de 1 kΩ em série (R-22):
/// limita a corrente caso o módulo apresente nível de 5 V no `RX`.
constexpr unsigned kGpsTx = 0;   // pino físico 1
/// Pico recebe, GPS transmite.
constexpr unsigned kGpsRx = 1;   // pino físico 2

// ---------------------------------------------------------------------------
// Encoder rotativo KY-040
// ---------------------------------------------------------------------------
//
// Conferidos na placa física em 2026-09-17: a ordem de serigrafia do módulo é
// o INVERSO da que costuma aparecer documentada, e a revisão 2 do
// bom_schematic tinha a sequência trocada — o que colocaria 3,3 V e GND
// diretamente em dois GPIO.

constexpr unsigned kEncoderClk = 2;  // pino físico 4; 100 nF para GND
constexpr unsigned kEncoderDt  = 3;  // pino físico 5; 100 nF para GND
constexpr unsigned kEncoderSw  = 4;  // pino físico 6

// ---------------------------------------------------------------------------
// Buzzer
// ---------------------------------------------------------------------------

/// Base do transistor, por um resistor de 1 kΩ. O GPIO **não** fornece a
/// corrente do buzzer: é o RNF02.
constexpr unsigned kBuzzerBase = 5;  // pino físico 7

// ---------------------------------------------------------------------------
// LED RGB — ânodo comum
// ---------------------------------------------------------------------------
//
// ⚠️ O ânodo comum vai ao `k3V3Out`, e cada cátodo desce por seu resistor até
// o GPIO: nível BAIXO acende. Ver R-33 e `LedRgbAnodoComum`.
// Os resistores diferem por canal — 330 Ω no vermelho, 68 Ω no verde e no
// azul — então a mesma intensidade numérica não dá o mesmo brilho percebido.

constexpr unsigned kLedVermelho = 6;  // pino físico  9; resistor 330 Ω
constexpr unsigned kLedVerde    = 7;  // pino físico 10; resistor  68 Ω
constexpr unsigned kLedAzul     = 8;  // pino físico 11; resistor  68 Ω

// ---------------------------------------------------------------------------
// Cartão microSD
// ---------------------------------------------------------------------------

/// Card detect. Entrada **sem pull interno**: a placa Adafruit já traz pull-up
/// de 4,7 kΩ para 3 V, e cartão presente = nível **ALTO**. Permite ao RF07
/// distinguir cartão ausente, que o motorista resolve, de cartão ilegível,
/// que ele não resolve dirigindo.
constexpr unsigned kSdDet = 14;  // pino físico 19
constexpr unsigned kSdCs  = 17;  // pino físico 22

// ---------------------------------------------------------------------------
// Display — SPI0, compartilhado com o cartão
// ---------------------------------------------------------------------------
//
// ⚠️ O barramento é COMPARTILHADO e exige mutex: nenhum acesso pode se
// intercalar com o outro. O cartão exige clock <= 400 kHz na inicialização e o
// display opera em dezenas de MHz, então a velocidade é reconfigurada por
// dispositivo antes de cada transação (RNF06).

constexpr unsigned kSpiMiso = 16;  // pino físico 21; cartão DO/SO
constexpr unsigned kSpiSck  = 18;  // pino físico 24; cartão CLK + display SCL
constexpr unsigned kSpiMosi = 19;  // pino físico 25; cartão CMD/SI + display SDA

constexpr unsigned kDisplayCs  = 20;  // pino físico 26
constexpr unsigned kDisplayDc  = 21;  // pino físico 27
constexpr unsigned kDisplayRst = 22;  // pino físico 29

/// PWM do backlight. Frequência >= 20 kHz: abaixo de ~1 kHz o painel cintila
/// de forma perceptível na visão periférica e pode dar efeito estroboscópico
/// com feições da estrada, e entre 1 e 20 kHz alguns módulos assobiam. A curva
/// de brilho é perceptual, não linear (requirements.md §4.1).
constexpr unsigned kDisplayBacklight = 15;  // pino físico 20

// ---------------------------------------------------------------------------
// Pinos de alimentação — NÃO SÃO GPIO
// ---------------------------------------------------------------------------
//
// Estão aqui como documentação, para que o número não seja procurado em outro
// arquivo. Não passe nenhum deles a `gpio_init()`.

/// Saída de 3,3 V do regulador da placa. É **a fonte**: leitor SD, encoder,
/// ânodo comum do LED e, se o módulo não tiver regulador próprio, o VCC do
/// display.
constexpr unsigned kPinoFisico3V3Out = 36;

/// ⚠️ **Entrada de *enable* do regulador, não uma fonte.** Pino **adjacente**
/// ao `3V3_OUT`, com pull-up de 100 kΩ para `VSYS` na placa. Puxá-lo ao GND
/// desliga o regulador de 3,3 V — e o RP2350 com ele.
///
/// Está declarado aqui **só para ser reconhecido e evitado**. O sintoma de
/// ligar uma carga nele é a placa dar boot e morrer ao acender o LED, o que
/// parece bug de firmware.
constexpr unsigned kPinoFisico3V3En = 37;

/// Entrada de 5 V, depois do Schottky. **Nunca** `VBUS` (pino 40), que é
/// ligado direto ao conector USB — ver R-01.
constexpr unsigned kPinoFisicoVsys = 39;
/// O Pico tem oito pinos de GND: 3, 8, 13, 18, 23, 28, 33 e 38.
constexpr unsigned kPinoFisicoGnd = 38;

// ---------------------------------------------------------------------------
// Invariantes guardadas pelo compilador
// ---------------------------------------------------------------------------

/// Todos os GPIO que o firmware usa. Acrescentar um pino aqui é obrigatório:
/// é esta lista que as verificações abaixo enxergam.
constexpr unsigned kTodosOsGpio[] = {
    kGpsTx,     kGpsRx,      kEncoderClk, kEncoderDt,  kEncoderSw,
    kBuzzerBase, kLedVermelho, kLedVerde,  kLedAzul,   kSdDet,
    kSdCs,      kSpiMiso,    kSpiSck,     kSpiMosi,    kDisplayCs,
    kDisplayDc, kDisplayRst, kDisplayBacklight,
};
constexpr std::size_t kQuantosGpio =
    sizeof kTodosOsGpio / sizeof *kTodosOsGpio;

namespace detalhe {

constexpr bool sem_repeticao(const unsigned* v, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (v[i] == v[j]) {
                return false;
            }
        }
    }
    return true;
}

/// O RP2350 tem GPIO 0 a 29, mas o cabeçalho do Pico expõe só 0–22, 26, 27 e
/// 28. Os quatro que faltam não são livres: **23, 24, 25 e 29 são do módulo
/// Wi-Fi CYW43** na variante W, e o projeto usa Wi-Fi para a atualização OTA.
constexpr bool disponivel_no_cabecalho(unsigned g) {
    return g <= 22 || g == 26 || g == 27 || g == 28;
}

constexpr bool todos_disponiveis(const unsigned* v, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (!disponivel_no_cabecalho(v[i])) {
            return false;
        }
    }
    return true;
}

}  // namespace detalhe

// Um pino atribuído duas vezes é o erro que mais custa a achar numa placa
// montada: dois periféricos disputam a linha e os dois funcionam pela metade.
// O compilador passa a recusar antes de existir placa.
static_assert(detalhe::sem_repeticao(kTodosOsGpio, kQuantosGpio),
              "dois periféricos foram atribuídos ao mesmo GPIO");

static_assert(detalhe::todos_disponiveis(kTodosOsGpio, kQuantosGpio),
              "algum GPIO não existe no cabeçalho do Pico, ou pertence ao "
              "módulo Wi-Fi CYW43 (23, 24, 25, 29)");

static_assert(kQuantosGpio == 18,
              "a contagem de GPIO mudou: confira o bom_schematic.md e a "
              "netlist do gera_fritzing.py antes de ajustar este número");

}  // namespace coruja::pinos
