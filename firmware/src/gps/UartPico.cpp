#include "gps/UartPico.h"

#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/uart.h>

#include "placa/Pinos.h"

namespace coruja {
namespace {
// O NEO-M8N sai de fábrica em 9600 8N1 sem autobaud (RF01.2).
// `uart0` e macro que expande para reinterpret_cast: nao serve em constexpr.
uart_inst_t* const kPorta = uart0;

/// A instância que a interrupção serve. Só há uma UART de GPS, e o vetor de
/// interrupção não carrega contexto — daí o ponteiro de arquivo.
UartPico* g_instancia = nullptr;
}  // namespace

UartPico::UartPico(std::uint32_t baud_inicial) {
    gpio_set_function(pinos::kGpsTx, GPIO_FUNC_UART);
    gpio_set_function(pinos::kGpsRx, GPIO_FUNC_UART);
    uart_init(kPorta, baud_inicial);
    uart_set_format(kPorta, 8, 1, UART_PARITY_NONE);
    // Sem controle de fluxo: o módulo não tem RTS/CTS ligados nesta placa.
    uart_set_hw_flow(kPorta, false, false);
    // FIFO ligada: a 115200 chegam ~11,5 kB/s, e um laço a 4 Hz que perdesse
    // bytes entre voltas truncaria sentenças — que é a causa 2 do RF01.5.
    uart_set_fifo_enabled(kPorta, true);
    baud_ = baud_inicial;

    // 🔴 **A recepção passa a ser por interrupção (26/10/2026).**
    //
    // Até aqui o `le()` drenava o FIFO de hardware direto, e o laço só o
    // visitava entre uma volta e outra. O FIFO tem 32 bytes e transborda em
    // **2,8 ms** a 115200; um redesenho da velocidade bloqueia o SPI por
    // ~53 ms. A diferença saía em sentenças truncadas — taxa de fixes medida
    // em 3,7 Hz em vez de 4,0, caindo a 2,7 Hz, sempre "com posição
    // presente": o receptor entregava e nós jogávamos fora.
    //
    // Com a interrupção despejando no anel, o tempo do laço deixa de
    // importar.
    g_instancia = this;
    irq_set_exclusive_handler(UART0_IRQ, &UartPico::atende_rx);
    irq_set_enabled(UART0_IRQ, true);
    // RX e **RX timeout**: sem o timeout, os últimos bytes de uma rajada
    // ficariam no FIFO sem atingir o gatilho, e a sentença só sairia quando
    // a próxima começasse — um epoch inteiro de atraso.
    uart_set_irq_enables(kPorta, /*rx_has_data=*/true, /*tx_needs_data=*/false);
}

void UartPico::atende_rx() {
    if (g_instancia == nullptr) { return; }
    // Esvazia o FIFO inteiro: voltar depois custaria outra entrada de
    // interrupção por byte.
    while (uart_is_readable(kPorta)) {
        g_instancia->anel_.escreve(uart_getc(kPorta));
    }
}

void UartPico::escreve(const std::uint8_t* bytes, std::size_t tamanho) {
    uart_write_blocking(kPorta, bytes, tamanho);
}

std::size_t UartPico::le(std::uint8_t* destino, std::size_t capacidade) {
    return anel_.le(destino, capacidade);
}

void UartPico::define_baud(std::uint32_t baud) {
    // Espera a fila de saída esvaziar antes de trocar: o último quadro ainda
    // pode estar saindo, e trocar no meio o corromperia — justamente o
    // `CFG-PRT`, que é o comando que pede a troca.
    uart_tx_wait_blocking(kPorta);
    baud_ = uart_set_baudrate(kPorta, baud);
    // ⚠️ **O que chegou no baud velho é lixo no novo.** Durante a sondagem
    // isto é meia sentença ilegível; deixá-la no anel faria o parser gastar
    // o começo da primeira sentença boa tentando fechá-la.
    std::uint8_t descarte[AnelBytes::kTamanho];
    anel_.le(descarte, sizeof descarte);
}

}  // namespace coruja
