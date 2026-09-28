#include "gps/UartPico.h"

#include <hardware/gpio.h>
#include <hardware/uart.h>

#include "placa/Pinos.h"

namespace coruja {
namespace {
// O NEO-M8N sai de fábrica em 9600 8N1 sem autobaud (RF01.2).
// `uart0` e macro que expande para reinterpret_cast: nao serve em constexpr.
uart_inst_t* const kPorta = uart0;
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
}

void UartPico::escreve(const std::uint8_t* bytes, std::size_t tamanho) {
    uart_write_blocking(kPorta, bytes, tamanho);
}

std::size_t UartPico::le(std::uint8_t* destino, std::size_t capacidade) {
    std::size_t n = 0;
    while (n < capacidade && uart_is_readable(kPorta)) {
        destino[n++] = uart_getc(kPorta);
    }
    return n;
}

void UartPico::define_baud(std::uint32_t baud) {
    // Espera a fila de saída esvaziar antes de trocar: o último quadro ainda
    // pode estar saindo, e trocar no meio o corromperia — justamente o
    // `CFG-PRT`, que é o comando que pede a troca.
    uart_tx_wait_blocking(kPorta);
    baud_ = uart_set_baudrate(kPorta, baud);
}

}  // namespace coruja
