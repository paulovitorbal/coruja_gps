#pragma once
#include <cstdint>

#include "encoder/AntiRepique.h"
#include "encoder/DecodificadorQuadratura.h"
#include "encoder/Encoder.h"

namespace coruja {

/// KY-040 lido por polling, delegando toda a decisão à lógica pura.
///
/// Esta classe é fina de propósito: ela lê três pinos e consulta o relógio, e
/// tudo que decide direção ou filtra repique está em
/// `DecodificadorQuadratura` e `AntiRepique`, que são testados no host. É a
/// regra do `docs/adr/0001` aplicada.
///
/// Pinos conforme `bom_schematic.md` §4, **conferidos na placa física** — a
/// ordem de serigrafia do módulo saiu invertida da que costuma ser
/// documentada, e a revisão anterior do documento colocava 3,3 V e GND
/// diretamente em dois GPIO.
class EncoderKy040 final : public Encoder {
public:
    static constexpr unsigned kGpioClk = 2;
    static constexpr unsigned kGpioDt  = 3;
    static constexpr unsigned kGpioSw  = 4;

    /// `pull_up_interno` liga o pull-up do Pico nos três pinos. O módulo
    /// KY-040 costuma trazer pull-up de 10 kΩ em `CLK` e `DT` mas **não** em
    /// `SW`; o interno do RP2350 é de ~50 a 80 kΩ, então onde já existe um o
    /// paralelo é praticamente inócuo, e onde não existe ele é necessário.
    /// Confirmar na bancada qual é o caso desta placa.
    ///
    /// `invertido` troca esquerda e direita, caso `CLK` e `DT` estejam
    /// trocados em relação ao esperado.
    explicit EncoderKy040(bool pull_up_interno = true, bool invertido = false,
                          unsigned gpio_clk = kGpioClk,
                          unsigned gpio_dt = kGpioDt,
                          unsigned gpio_sw = kGpioSw);

    /// Amostra os pinos e devolve o evento resultante. Chamar com frequência:
    /// a decodificação por tabela precisa ver cada transição, e a 4 Hz do
    /// laço principal um giro rápido seria perdido. Ver a nota de taxa abaixo.
    EventoEncoder proximo_evento() override;

private:
    unsigned gpio_clk_;
    unsigned gpio_dt_;
    unsigned gpio_sw_;

    DecodificadorQuadratura decodificador_;
    AntiRepique             chave_;
};

}  // namespace coruja
