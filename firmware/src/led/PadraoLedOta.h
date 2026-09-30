#pragma once
#include <cstdint>

#include "led/Cor.h"
#include "rede/ObservadorOta.h"

namespace coruja {

/// O LED durante a atualização, acompanhando o que a tela mostra.
///
/// **Ciano para tudo que é trabalho, e o ritmo diz o quê.** A cor separa o
/// OTA de qualquer estado de via; a cadência separa as fases entre si, sem
/// gastar matiz novo — que é o recurso escasso, porque o §4.1 já tem cinco
/// em uso e exige 40° de separação entre eles.
///
/// | Fase | LED |
/// | :--- | :--- |
/// | conectando, consultando | ciano, 1 Hz — começou |
/// | baixando | ciano, 2 Hz — trabalho em curso |
/// | verificando, gravando | **ciano fixo — não desligue** |
/// | atualizada, já em dia | verde fixo |
/// | falhou | vermelho, 2 Hz |
///
/// O fixo na gravação não é falta de ideia: é a janela da troca atômica do
/// RF05.2, o único momento em que desligar o aparelho tem consequência. Luz
/// parada se lê como "ocupado, não toque"; luz piscando convida a mexer.
class PadraoLedOta {
public:
    void define_fase(FaseOta fase, std::uint32_t agora_ms);

    /// A cor agora. Chamar com frequência: é daqui que sai o piscar.
    Cor cor(std::uint32_t agora_ms) const;

    FaseOta fase() const { return fase_; }

private:
    FaseOta       fase_ = FaseOta::Conectando;
    std::uint32_t desde_ms_ = 0;
};

}  // namespace coruja
