#pragma once
#include <cstdint>

namespace coruja {

/// Espera bloqueante, injetada.
///
/// Parece exagero abstrair um `sleep_ms`, e não é: era a **última** amarra de
/// hardware na lógica de atualização. As três tentativas do RF05.2 esperam
/// 5 s entre si, e um teste que dormisse de verdade levaria 10 segundos para
/// exercitar o caminho de falha — a ponto de ninguém rodar. Com a espera
/// injetada, o teste **verifica** que esperou, e em quanto, sem esperar.
class Pausa {
public:
    virtual ~Pausa() = default;
    virtual void espera_ms(std::uint32_t ms) = 0;

    /// Milissegundos desde o boot.
    ///
    /// **Entrou junto da espera, e não numa porta própria.** Quem precisa
    /// esperar quase sempre precisa saber que horas são, e as duas se
    /// implementam na mesma linha do SDK. Uma interface separada custaria
    /// mais um dublê em cada teste sem separar responsabilidade nenhuma:
    /// ambas são "o tempo, visto de fora".
    ///
    /// O caso que a motivou: o LED pisca durante o OTA, e o piscar é função
    /// do tempo. Sem isto, a cor só seria recalculada nas mudanças de fase
    /// e o LED ficaria parado durante todo um download de 214 KB.
    virtual std::uint32_t agora_ms() = 0;
};

}  // namespace coruja
