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
/// Clock do SPI do display: **16 MHz, medidos e não supostos.**
///
/// A varredura da bancada mostrou desenho limpo até 25 MHz efetivos nesta
/// protoboard (autor, 2026-09-29); 16 deixa margem para variação de
/// temperatura e de contato.
///
/// Os 4 MHz que estavam aqui antes eram conservadorismo caro. Cada passo de
/// rolagem redesenha uma linha de texto — 320 × 20 px, 12.800 bytes — e a
/// 4 MHz isso é **25,6 ms de SPI bloqueante**, mais que a folga inteira de
/// 5,75 ms que a decodificação do encoder tem no estado `(0,0)` (R-36). A
/// 16 MHz cai para 6,4 ms, e com rolagem por caractere (4 passos/s) o custo
/// total fica em 3% do barramento.
///
/// O número é do **meio físico**, não do chip: o ST7789V aceita ~66 MHz,
/// protoboard com fio jumper não. Foi por confundir os dois que a primeira
/// versão saiu com 62,5 MHz.
constexpr std::uint32_t kBaudDisplayHz = 16000000;

class PainelSt7789 {
public:
    /// Reset por hardware, sequencia de inicializacao e limpeza da tela.
    ///
    /// **Nao liga a retroiluminacao.** Quem chama acende depois de
    /// desenhar o primeiro quadro, senao o usuario ve um retangulo de
    /// lixo de RAM antes da primeira tela.
    /// `baud_hz` vem de `kBaudDisplayHz`; ver a nota de lá.
    ///
    /// `modo3` troca CPOL/CPHA de 0,0 para 1,1. O ST7789V amostra o
    /// SDA na borda de SUBIDA, e os dois modos entregam isso -- por
    /// isso as bibliotecas se dividem entre eles. Exposto para a
    /// bancada eliminar a duvida medindo, nao lendo.
    void inicia(std::uint32_t baud_hz = kBaudDisplayHz,
                bool modo3 = false);

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

    /// Desenha um mapa de bits de 1 bpp, expandindo para RGB565 em fluxo.
    ///
    /// **Uma janela por glifo, nao um pixel por vez.** Pintar 56x94 com
    /// `preenche(x, y, 1, 1, ...)` seriam 5264 transacoes SPI, cada uma com
    /// CASET, RASET e RAMWR proprios -- ordens de magnitude mais lento que
    /// abrir a janela uma vez e despejar as linhas.
    ///
    /// `bits` tem `bytes_por_linha * altura` bytes, MSB primeiro: o bit 7
    /// do primeiro byte de cada linha e o pixel da esquerda.
    ///
    /// **Recorta em coluna, inclusive parcialmente**, porque o texto que
    /// nao cabe rola e precisa de glifos meio de fora. Fora da tela por
    /// inteiro, nao desenha nada.
    void desenha_bitmap(int x, int y, int largura, int altura,
                        int bytes_por_linha, const std::uint8_t* bits,
                        std::uint16_t cor, std::uint16_t fundo);

    /// Desenha um bloco de pixels RGB565 ja prontos (sprite de icone).
    ///
    /// Mesma janela unica do `desenha_bitmap`, sem expansao: os pixels vao
    /// direto. `pixels` tem `largura * altura` valores, linha por linha de
    /// cima para baixo.
    void desenha_rgb565(int x, int y, int largura, int altura,
                        const std::uint16_t* pixels);

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
