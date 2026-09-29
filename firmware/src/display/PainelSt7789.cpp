#include "display/PainelSt7789.h"

#include <initializer_list>

#include <hardware/gpio.h>
#include <hardware/spi.h>
#include <pico/time.h>

#include "display/Visor.h"
#include "placa/Pinos.h"

namespace coruja {

namespace {

// --- comandos do ST7789V usados aqui ---
constexpr std::uint8_t kSwreset = 0x01;
constexpr std::uint8_t kSlpout  = 0x11;
constexpr std::uint8_t kInvoff  = 0x20;
constexpr std::uint8_t kInvon   = 0x21;
constexpr std::uint8_t kDispon  = 0x29;
constexpr std::uint8_t kCaset   = 0x2A;
constexpr std::uint8_t kRaset   = 0x2B;
constexpr std::uint8_t kRamwr   = 0x2C;
constexpr std::uint8_t kMadctl  = 0x36;
constexpr std::uint8_t kColmod  = 0x3A;
constexpr std::uint8_t kNoron   = 0x13;

/// MADCTL: MV (troca eixos) + MX (espelha colunas) poe o painel retrato de
/// 240x320 em paisagem de 320x240, com a origem no canto superior esquerdo
/// visto na horizontal. Sem MV o quadro sai em pe; so com MV, invertido.
constexpr std::uint8_t kMadctlPaisagem = 0x60;  // MV | MX

/// 16 bits por pixel (RGB565), que e o formato do `Cor565`.
constexpr std::uint8_t kColmod16Bpp = 0x55;

void seleciona(bool ligado) {
    gpio_put(pinos::kDisplayCs, !ligado);  // CS e ativo em nivel baixo
}

}  // namespace

void PainelSt7789::escreve(std::uint8_t comando, const std::uint8_t* params,
                           std::size_t n) {
    // Comando e parametros numa transacao so, com CS baixo do inicio ao
    // fim. O datasheet trata CS como delimitador de quadro, e ha paineis
    // que descartam o comando inteiro se ele subir no meio -- mais um modo
    // de falhar em silencio absoluto num modulo sem MISO (R-62).
    seleciona(true);
    gpio_put(pinos::kDisplayDc, false);  // DC baixo = comando
    spi_write_blocking(spi1, &comando, 1);
    if (n > 0) {
        gpio_put(pinos::kDisplayDc, true);  // DC alto = dados
        spi_write_blocking(spi1, params, n);
    }
    seleciona(false);
}

void PainelSt7789::escreve(std::uint8_t comando, std::uint8_t param) {
    escreve(comando, &param, 1);
}

void PainelSt7789::define_baud(std::uint32_t hz) {
    baud_efetivo_ = spi_set_baudrate(spi1, hz);
}

void PainelSt7789::inicia(std::uint32_t baud_hz, bool modo3) {
    spi_init(spi1, baud_hz);
    baud_efetivo_ = spi_set_baudrate(spi1, baud_hz);
    spi_set_format(spi1, 8, modo3 ? SPI_CPOL_1 : SPI_CPOL_0,
                   modo3 ? SPI_CPHA_1 : SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(pinos::kDisplaySck, GPIO_FUNC_SPI);
    gpio_set_function(pinos::kDisplayMosi, GPIO_FUNC_SPI);

    for (unsigned p : {pinos::kDisplayCs, pinos::kDisplayDc,
                       pinos::kDisplayRst}) {
        gpio_init(p);
        gpio_set_dir(p, GPIO_OUT);
        gpio_put(p, true);
    }

    // Reset por hardware. Os tempos sao do datasheet do ST7789V: o pulso
    // baixo precisa de us, e depois 120 ms antes de qualquer comando --
    // pular essa espera e a causa classica de tela preta que some quando
    // se poe um printf no meio.
    gpio_put(pinos::kDisplayRst, false);
    sleep_us(20);
    gpio_put(pinos::kDisplayRst, true);
    sleep_ms(120);

    // Os atrasos sao os das implementacoes de campo, nao os minimos do
    // datasheet. A primeira versao usava o minimo (120 ms no SLPOUT, 20 ms
    // no DISPON) e a tela nao acendeu: o que o papel garante e o que o
    // vidro real precisa nao sao a mesma coisa, e errar para mais custa
    // meio segundo no boot uma vez.
    escreve(kSwreset, nullptr, 0);
    sleep_ms(150);
    escreve(kSlpout, nullptr, 0);
    sleep_ms(500);

    escreve(kColmod, kColmod16Bpp);
    sleep_ms(10);
    escreve(kMadctl, kMadctlPaisagem);
    escreve(kInvon, nullptr, 0);  // IPS com ST7789 costuma precisar (R-62)
    // NORON faltava na primeira versao. Toda implementacao madura o envia
    // antes do DISPON; sem ele o painel pode ficar em modo parcial.
    escreve(kNoron, nullptr, 0);
    sleep_ms(10);

    limpa(paleta::kFundo);
    escreve(kDispon, nullptr, 0);
    sleep_ms(500);
}

void PainelSt7789::define_inversao(bool invertido) {
    escreve(invertido ? kInvon : kInvoff, nullptr, 0);
}

void PainelSt7789::preenche(int x, int y, int largura, int altura,
                            std::uint16_t cor) {
    // Recorta antes de tocar no controlador. Uma janela com coordenada
    // fora da tela nao da erro visivel: ela embaralha o resto do quadro.
    if (x < 0) { largura += x; x = 0; }
    if (y < 0) { altura += y; y = 0; }
    if (x + largura > tela::kLargura) { largura = tela::kLargura - x; }
    if (y + altura > tela::kAltura)   { altura = tela::kAltura - y; }
    if (largura <= 0 || altura <= 0) { return; }

    const int x1 = x + largura - 1;
    const int y1 = y + altura - 1;
    const std::uint8_t col[4] = {
        static_cast<std::uint8_t>(x >> 8),  static_cast<std::uint8_t>(x),
        static_cast<std::uint8_t>(x1 >> 8), static_cast<std::uint8_t>(x1)};
    const std::uint8_t lin[4] = {
        static_cast<std::uint8_t>(y >> 8),  static_cast<std::uint8_t>(y),
        static_cast<std::uint8_t>(y1 >> 8), static_cast<std::uint8_t>(y1)};
    escreve(kCaset, col, sizeof col);
    escreve(kRaset, lin, sizeof lin);

    // Um buffer de uma linha, reenviado. A tela inteira sao 150 KB, que o
    // RNF07 chegou a reservar e a §4.1 mostrou ser desnecessario: o layout
    // nao muda e nada pisca, entao se desenha por regiao.
    std::uint8_t linha[tela::kLargura * 2];
    const std::uint8_t alto = static_cast<std::uint8_t>(cor >> 8);
    const std::uint8_t baixo = static_cast<std::uint8_t>(cor);
    for (int i = 0; i < largura; ++i) {
        linha[i * 2] = alto;      // RGB565 vai em big-endian no barramento
        linha[i * 2 + 1] = baixo;
    }

    // RAMWR e os pixels tambem ficam numa transacao so.
    seleciona(true);
    gpio_put(pinos::kDisplayDc, false);
    spi_write_blocking(spi1, &kRamwr, 1);
    gpio_put(pinos::kDisplayDc, true);
    for (int l = 0; l < altura; ++l) {
        spi_write_blocking(spi1, linha,
                           static_cast<std::size_t>(largura) * 2);
    }
    seleciona(false);
}

void PainelSt7789::limpa(std::uint16_t cor) {
    preenche(0, 0, tela::kLargura, tela::kAltura, cor);
}

}  // namespace coruja
