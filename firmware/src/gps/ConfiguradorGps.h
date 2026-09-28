#pragma once
#include <cstdint>

#include "gps/Uart.h"
#include "nucleo/Pausa.h"
#include "nucleo/Ubx.h"

namespace coruja {

class Logger;

enum class ResultadoConfigGps : std::uint8_t {
    Configurado,      ///< tudo aceito e persistido
    SemResposta,      ///< o módulo não respondeu a nada: fiação, energia, baud
    Recusado,         ///< respondeu NAK a um comando
    NaoPersistiu,     ///< configurou, mas o `CFG-CFG` não pegou
};

const char* descreve(ResultadoConfigGps r);

/// Quantas vezes se relê a porta esperando um ACK antes de desistir.
constexpr unsigned kEsperasPorAck = 50;
/// Pausa entre releituras. 50 × 20 ms = 1 s por comando, com folga sobre o
/// tempo de resposta do módulo, que é de milissegundos.
constexpr std::uint32_t kEsperaEntreLeiturasMs = 20;
/// Cada comando é tentado duas vezes antes de o passo falhar.
constexpr unsigned kTentativasPorComando = 2;

constexpr std::uint32_t kBaudFabrica = 9600;
constexpr std::uint32_t kBaudDesejado = 115200;
constexpr std::uint16_t kPeriodoNavegacaoMs = 250;   // 4 Hz, RF01.4

/// Aplica a configuração obrigatória do RF01.2 e confirma cada passo pelo ACK.
///
/// **Confirmar é o ponto.** Mandar quadros e torcer é o que produz o sintoma
/// que o RF01.5 descreve: o módulo segue em 1 Hz com uma sentença por época,
/// e o defeito só aparece meses depois como "taxa baixa". Aqui cada comando
/// espera o `ACK-ACK` da sua classe e id, e um `ACK-NAK` é falha explícita.
///
/// Sem hardware: recebe `Uart` e `Pausa`. É o que permite exercitar no host
/// o módulo que recusa, o que não responde, e a troca de baud no meio.
class ConfiguradorGps {
public:
    ConfiguradorGps(Uart& uart, Pausa& pausa) : uart_(uart), pausa_(pausa) {}

    ResultadoConfigGps executa(Logger& log);

    /// Baud em que a porta ficou. Interessa a quem for ler depois.
    std::uint32_t baud_final() const { return baud_final_; }

private:
    /// Desfecho de um comando. Silêncio e recusa **não** são a mesma coisa:
    /// um aponta para fiação, energia ou baud errado; o outro diz que o
    /// módulo entendeu e não aceitou. Misturá-los manda procurar no lugar
    /// errado.
    enum class Passo : std::uint8_t { Ok, Nak, Silencio };

    Passo comanda(const std::uint8_t* quadro, std::size_t tamanho,
                  std::uint8_t id, const char* nome, Logger& log);
    bool espera_ack(std::uint8_t id, Logger& log, bool* recusou);

    Uart&            uart_;
    Pausa&           pausa_;
    ubx::LeitorUbx   leitor_;
    std::uint32_t    baud_final_ = kBaudFabrica;
};

}  // namespace coruja
