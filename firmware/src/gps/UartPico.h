#pragma once
#include "gps/Uart.h"
#include "nucleo/AnelBytes.h"

namespace coruja {

/// Porte da UART do RP2350 para o NEO-M8N: `uart0` nos GPIO 0 e 1.
///
/// O TX do Pico vai ao RX do módulo **através do R5 de 1 kΩ** (R-22): o
/// módulo é de 3,3 V, mas a placa GY-GPSV3 tem divisor na entrada e o
/// resistor limita a corrente se algum dia entrar um módulo de 5 V.
class UartPico : public Uart {
public:
    /// Configura os pinos e abre a porta no baud de fábrica do módulo.
    explicit UartPico(std::uint32_t baud_inicial = 9600);

    void escreve(const std::uint8_t* bytes, std::size_t tamanho) override;
    std::size_t le(std::uint8_t* destino, std::size_t capacidade) override;
    void define_baud(std::uint32_t baud) override;

    std::uint32_t baud() const { return baud_; }

    /// Bytes descartados por anel cheio. **Zero é o estado saudável.**
    ///
    /// Diferente de zero significa que o laço ficou longe demais da serial —
    /// e diz, em bytes, o tamanho do atraso. Vai à linha `vigia`.
    std::uint32_t perdidos() const { return anel_.perdidos(); }
    void zera_perdidos() { anel_.zera_perdidos(); }

private:
    /// Chamado pela interrupção de recepção. Estático porque o vetor de
    /// interrupção não carrega contexto; só existe uma UART de GPS.
    static void atende_rx();

    std::uint32_t baud_ = 0;
    AnelBytes     anel_;
};

}  // namespace coruja
