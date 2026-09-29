#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Driver do painel ST7789V do `GMT024-08-SPI8P ver. 1.3` (R-62).
///
/// Camada baixa: janela e pixels, sem texto nem icone. O `Visor` completo
/// vem por cima disto quando as fontes existirem; separar permite ligar e
/// conferir o painel antes de ter um unico glifo desenhado.
///
/// **O modulo nao tem MISO** (R-62). Nada pode ser lido de volta: nem o
/// registrador de ID, nem o estado do controlador, nem se um comando foi
/// aceito. Toda escrita aqui e cega, e a unica verificacao possivel e
/// **olhar a tela**. E por isso que o `bancada_display` existe.
///
/// O painel e 240x320 em retrato; o firmware usa 320x240 deitado
/// (`Visor.h`), o que se obtem por MADCTL na inicializacao -- nao e um
/// painel nativo em paisagem.
class PainelSt7789 {
public:
    /// Reset por hardware, sequencia de inicializacao e limpeza da tela.
    ///
    /// **Nao liga a retroiluminacao.** Quem chama acende depois de
    /// desenhar o primeiro quadro, senao o usuario ve um retangulo de
    /// lixo de RAM antes da primeira tela.
    /// `baud_hz` e do **meio fisico**, nao do chip. O ST7789V aceita
    /// ~66 MHz; protoboard com fio jumper, nao. O padrao e conservador de
    /// proposito -- ver a nota em `kBaudBancadaHz`.
    /// `modo3` troca CPOL/CPHA de 0,0 para 1,1. O ST7789V amostra o
    /// SDA na borda de SUBIDA, e os dois modos entregam isso -- por
    /// isso as bibliotecas se dividem entre eles. Exposto para a
    /// bancada eliminar a duvida medindo, nao lendo.
    void inicia(std::uint32_t baud_hz = 4000000, bool modo3 = false);

    /// Troca a velocidade em operacao. Existe para achar o teto real
    /// medindo, e porque o microSD divide este barramento e exige
    /// <= 400 kHz na propria inicializacao.
    void define_baud(std::uint32_t hz);

    /// O que o divisor inteiro do RP2350 conseguiu de fato entregar.
    std::uint32_t baud_efetivo() const { return baud_efetivo_; }

    /// Preenche um retangulo. Recorta no limite da tela em vez de
    /// escrever fora: um x negativo vindo de um calculo de layout
    /// corromperia a janela do controlador e embaralharia o resto do
    /// quadro, que e um sintoma dificil de rastrear.
    void preenche(int x, int y, int largura, int altura, std::uint16_t cor);

    void limpa(std::uint16_t cor);

    /// Inverte as cores do painel (comando `INVON`/`INVOFF`).
    ///
    /// Painel IPS com ST7789 costuma precisar de `INVON`, mas isso
    /// depende do vidro e nao da para decidir no papel: ou as cores saem
    /// certas, ou saem todas complementares. Exposto para a bancada
    /// poder alternar e a pessoa escolher olhando.
    void define_inversao(bool invertido);

private:
    void escreve(std::uint8_t comando, const std::uint8_t* params,
                 std::size_t n);
    void escreve(std::uint8_t comando, std::uint8_t param);

    std::uint32_t baud_efetivo_ = 0;
};

}  // namespace coruja
