// Bit-bang do ST7789V: elimina o periferico SPI da equacao.
//
// **Nao usa nada do driver do projeto.** Nem PainelSt7789, nem spi_init,
// nem RetroiluminacaoPwm. So gpio_put, na unha. E deliberado: depois de
// quatro hipoteses erradas (velocidade, CS, alimentacao, sequencia de
// init), o que falta e separar "a configuracao do periferico SPI esta
// errada" de "o modulo nao responde a nada".
//
//   desenhou  -> o periferico estava mal configurado, e o defeito e meu,
//                localizado numa area pequena
//   nao desenhou -> nao e software. O proximo passo sai do firmware.
//
// O clock sai em ~300 kHz, lento de proposito: a esta altura o custo de
// esperar alguns segundos por quadro nao e nada perto do custo de mais
// uma duvida em aberto.

#include <cstdint>
#include <cstdio>

#include <hardware/gpio.h>
#include <pico/stdio_usb.h>
#include <pico/stdlib.h>

#include "placa/Pinos.h"

using namespace coruja;

namespace {

/// **Com CS e RST amarrados no hardware, o Pico nao toca neles.**
///
/// CS no GND e RST em 3,3 V deixam so DC, SCL e SDA sob controle do
/// software -- o minimo indispensavel do SPI de 4 fios. O DC nao tem como
/// sair: e ele que distingue comando de dado, e sem ele o mesmo byte 0x2C
/// seria "comando RAMWR" ou "pixel de valor 0x2C" sem nada para desempatar.
///
/// Serve para um caso especifico que nenhum teste anterior cobriu: um RST
/// presto em nivel baixo mantem o chip em reset permanente, e isso produz
/// exatamente o que vimos -- tela apagada, nenhum comando surtindo efeito,
/// em velocidade nenhuma, com a continuidade medindo perfeita. A
/// continuidade prova o fio, nao a tensao.
/// Voltou para `false`: a medicao mostrou RST em 3,24 V, entao o chip nao
/// esta preso em reset e nao ha por que tirar os dois pinos do Pico.
constexpr bool kCsRstNoHardware = false;

constexpr int kLargura = 320;
constexpr int kAltura = 240;

/// Meio periodo do clock. Puro atraso de laco, sem temporizador.
void meio_ciclo() {
    for (volatile int i = 0; i < 12; ++i) {
        asm volatile("nop");
    }
}

void pulso_clock() {
    meio_ciclo();
    gpio_put(pinos::kDisplaySck, true);
    meio_ciclo();
    gpio_put(pinos::kDisplaySck, false);
}

/// Modo 0, MSB primeiro: o dado muda com o clock baixo e o ST7789V
/// amostra na borda de subida.
void envia_byte(std::uint8_t b) {
    for (int i = 7; i >= 0; --i) {
        gpio_put(pinos::kDisplayMosi, ((b >> i) & 1) != 0);
        pulso_clock();
    }
}

void seleciona(bool ligado) {
    if (!kCsRstNoHardware) {
        gpio_put(pinos::kDisplayCs, !ligado);
    }
}

void comando(std::uint8_t c) {
    seleciona(true);
    gpio_put(pinos::kDisplayDc, false);
    envia_byte(c);
    seleciona(false);
}

void comando_com(std::uint8_t c, const std::uint8_t* p, int n) {
    seleciona(true);
    gpio_put(pinos::kDisplayDc, false);
    envia_byte(c);
    gpio_put(pinos::kDisplayDc, true);
    for (int i = 0; i < n; ++i) {
        envia_byte(p[i]);
    }
    seleciona(false);
}

void saida(unsigned pino, bool nivel) {
    gpio_set_function(pino, GPIO_FUNC_SIO);
    gpio_init(pino);
    gpio_set_dir(pino, GPIO_OUT);
    gpio_put(pino, nivel);
}

void preenche(std::uint16_t cor) {
    const std::uint8_t col[4] = {0, 0,
                                 static_cast<std::uint8_t>((kLargura - 1) >> 8),
                                 static_cast<std::uint8_t>(kLargura - 1)};
    const std::uint8_t lin[4] = {0, 0,
                                 static_cast<std::uint8_t>((kAltura - 1) >> 8),
                                 static_cast<std::uint8_t>(kAltura - 1)};
    comando_com(0x2A, col, 4);  // CASET
    comando_com(0x2B, lin, 4);  // RASET

    seleciona(true);
    gpio_put(pinos::kDisplayDc, false);
    envia_byte(0x2C);  // RAMWR
    gpio_put(pinos::kDisplayDc, true);
    const std::uint8_t alto = static_cast<std::uint8_t>(cor >> 8);
    const std::uint8_t baixo = static_cast<std::uint8_t>(cor);
    for (long i = 0; i < static_cast<long>(kLargura) * kAltura; ++i) {
        envia_byte(alto);
        envia_byte(baixo);
    }
    seleciona(false);
}

void inicializa_painel() {
    if (!kCsRstNoHardware) {
        gpio_put(pinos::kDisplayRst, false);
        sleep_us(50);
        gpio_put(pinos::kDisplayRst, true);
    }
    // Com RST em 3,3 V vale o reset interno de energizacao do ST7789V,
    // que e o que muitos modulos usam de fabrica.
    sleep_ms(150);

    comando(0x01);  // SWRESET
    sleep_ms(150);
    comando(0x11);  // SLPOUT
    sleep_ms(500);

    const std::uint8_t colmod = 0x55;  // 16 bpp
    comando_com(0x3A, &colmod, 1);
    sleep_ms(10);

    // --- tensoes internas do painel ---
    //
    // **Esta e a parte que faltava.** Os registradores abaixo definem as
    // tensoes que acionam o cristal liquido. Eles tem valores de fabrica
    // na NVM do chip, e em parte dos paineis esses valores nao servem: o
    // controlador recebe os dados, escreve na RAM certinho, e o vidro nao
    // muda de estado porque nao ha tensao para move-lo. Sinais perfeitos,
    // chip alimentado, tela imovel -- que e exatamente o que se mediu.
    //
    // Valores da sequencia de referencia da Sitronix para o ST7789V,
    // usada pela TFT_eSPI e pela Adafruit_ST7789.
    const std::uint8_t porctrl[5] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    comando_com(0xB2, porctrl, 5);          // PORCTRL
    const std::uint8_t gctrl = 0x35;
    comando_com(0xB7, &gctrl, 1);           // GCTRL
    const std::uint8_t vcoms = 0x19;
    comando_com(0xBB, &vcoms, 1);           // VCOMS ~0,725 V
    const std::uint8_t lcmctrl = 0x2C;
    comando_com(0xC0, &lcmctrl, 1);         // LCMCTRL
    const std::uint8_t vdvvrhen = 0x01;
    comando_com(0xC2, &vdvvrhen, 1);        // VDVVRHEN
    const std::uint8_t vrhs = 0x12;
    comando_com(0xC3, &vrhs, 1);            // VRHS ~4,45 V
    const std::uint8_t vdvs = 0x20;
    comando_com(0xC4, &vdvs, 1);            // VDVS
    const std::uint8_t frctrl2 = 0x0F;
    comando_com(0xC6, &frctrl2, 1);         // 60 Hz
    const std::uint8_t pwctrl1[2] = {0xA4, 0xA1};
    comando_com(0xD0, pwctrl1, 2);          // PWCTRL1
    const std::uint8_t gamma_pos[14] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B,
                                        0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B,
                                        0x1F, 0x23};
    comando_com(0xE0, gamma_pos, 14);
    const std::uint8_t gamma_neg[14] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C,
                                        0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F,
                                        0x20, 0x23};
    comando_com(0xE1, gamma_neg, 14);
    sleep_ms(10);
    const std::uint8_t madctl = 0x60;  // MV | MX -> paisagem
    comando_com(0x36, &madctl, 1);
    comando(0x21);  // INVON
    comando(0x13);  // NORON
    sleep_ms(10);
    comando(0x29);  // DISPON
    sleep_ms(500);
}

}  // namespace

int main() {
    stdio_init_all();
    for (int i = 0; i < 300 && !stdio_usb_connected(); ++i) {
        sleep_ms(100);
    }
    sleep_ms(300);

    std::printf("\ncoruja - bit-bang do ST7789V (sem periferico SPI)\n");

    // Backlight direto, sem PWM: um GPIO alto satura o SS8050.
    saida(pinos::kDisplayBacklight, true);
    saida(pinos::kDisplayDc, true);
    if (!kCsRstNoHardware) {
        saida(pinos::kDisplayCs, true);
        saida(pinos::kDisplayRst, true);
    } else {
        // Deixa os dois em alta impedancia: quem manda e o fio.
        gpio_set_function(pinos::kDisplayCs, GPIO_FUNC_SIO);
        gpio_init(pinos::kDisplayCs);
        gpio_set_dir(pinos::kDisplayCs, GPIO_IN);
        gpio_set_function(pinos::kDisplayRst, GPIO_FUNC_SIO);
        gpio_init(pinos::kDisplayRst);
        gpio_set_dir(pinos::kDisplayRst, GPIO_IN);
        std::printf("CS e RST em alta impedancia: amarre CS no GND e\n"
                    "RST em 3,3 V, tirando os dois dos GPIO 20 e 22.\n");
    }
    saida(pinos::kDisplaySck, false);   // modo 0: clock em repouso BAIXO
    saida(pinos::kDisplayMosi, false);

    // O LED do projeto e de anodo comum: nivel BAIXO acende. Aqui vai na
    // unha tambem, para nao depender de nenhuma classe do projeto.
    saida(pinos::kLedVermelho, true);
    saida(pinos::kLedVerde, true);
    saida(pinos::kLedAzul, true);

    std::printf("inicializando o painel...\n");
    inicializa_painel();

    const struct { const char* nome; std::uint16_t cor; unsigned led; }
    passos[] = {
        {"VERMELHO", 0xF800, pinos::kLedVermelho},
        {"VERDE",    0x07E0, pinos::kLedVerde},
        {"AZUL",     0x001F, pinos::kLedAzul},
    };

    std::printf("cada preenchimento leva alguns segundos (clock lento)\n");
    while (true) {
        for (const auto& p : passos) {
            gpio_put(pinos::kLedVermelho, true);
            gpio_put(pinos::kLedVerde, true);
            gpio_put(pinos::kLedAzul, true);
            gpio_put(p.led, false);  // acende so o canal da vez
            std::printf("  %s\n", p.nome);
            preenche(p.cor);
            sleep_ms(1500);
        }
    }
}
