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

/// Voltas de espera da SONDAGEM de baud, contra `kEsperasPorAck` do comando.
///
/// ⚠️ **São poucas de propósito.** A janela longa existe para quadro
/// perdido; no baud errado não há quadro perdido nenhum — o módulo está
/// fisicamente falando outra velocidade, e esperar mais não o faz responder.
/// A 9600 um `ACK` de `CFG-RATE` são ~10 bytes, uns 10 ms de linha; 240 ms
/// dão vinte e tantas vezes de folga sobre isso.
///
/// Medido em 15 e 16/10/2026: a sondagem antiga — duas tentativas longas em
/// cada baud — custava 1,74 s em toda partida morna, com o módulo fixando em
/// 0,5 s do outro lado.
constexpr unsigned kEsperasDaSondagem = 12;

/// Quantas vezes a sondagem pergunta em CADA baud antes de passar ao
/// seguinte.
///
/// Dois, por medida de campo: ver o comentario em `ConfiguradorGps::sonda`.
/// A segunda pergunta so acontece quando a primeira ficou sem resposta, entao
/// o modulo que responde de primeira nao paga nada por ela.
constexpr unsigned kSondagensPorBaud = 2;

/// O período de navegação de FÁBRICA, em ms. É com ele que se sonda.
///
/// 🔴 **A sonda não pode pedir 4 Hz.** Até 2026-10-26 ela pedia, e o efeito
/// está medido no diário de 24/10: com o módulo ainda a 9600 e com as sete
/// sentenças de fábrica ligadas, subir para 4 Hz pede **~1 960 B/s de um
/// enlace que entrega 960**. A linha satura, o ACK do `CFG-MSG` seguinte se
/// perde na enxurrada, a sequência aborta — e o aparelho roda a viagem
/// inteira assim. Foram 487 s a 1,3 Hz naquele sábado.
///
/// Pedir 1 Hz é pedir o que já está lá: o ACK confirma o baud sem mexer em
/// carga nenhuma.
constexpr std::uint32_t kPeriodoFabricaMs = 1000;

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

    /// O que a partida tem a dizer, numa linha so.
    ///
    /// 🔴 **Existe porque o recital custava 66% do diario.** No historico de
    /// 108 partidas, `gps:` ocupava 2 552 das 4 517 linhas e 116 KB dos 321 --
    /// quase tudo "CFG-ALGUMA COISA: ACK", identico em toda partida, dizendo
    /// que o passo que devia funcionar funcionou. A `vigia`, unica linha que
    /// fala da viagem, era 3%. O passo a passo continua em DEBUG, para a
    /// bancada; o campo recebe este resumo.
    struct Resumo {
        /// Em que baud o modulo apareceu. Diz se a BBR sobreviveu, e e o que
        /// explica a sondagem ter custado 2 s ou 60 ms.
        std::uint32_t baud_encontrado = 0;
        bool          achou = false;
        /// A sondagem precisou da fase insistente?
        bool          insistiu = false;
        /// Passos opcionais que o modulo recusou. Este NEO-M8N recusa a TXT
        /// em toda partida; contar e melhor que avisar, porque aviso que
        /// sempre acontece nao avisa nada.
        unsigned      opcionais_recusados = 0;
        /// Nome do ultimo passo tentado. Em falha, e onde a sequencia parou.
        const char*   passo = "";
        /// Quanto a sequencia inteira levou, em ms.
        std::uint32_t gasto_ms = 0;
    };
    const Resumo& resumo() const { return resumo_; }

    /// Roda a sequência de novo quando ela aborta. Devolve o último desfecho.
    ///
    /// 🔴 **Existe por causa de 24/10/2026.** A sequência abortou no segundo
    /// comando, o aparelho seguiu em frente, e a viagem inteira correu com o
    /// receptor meio configurado — 487 s a 1,3 Hz, com as sete sentenças de
    /// fábrica saturando um enlace de 9600. Desistir na primeira era aceitar
    /// isso em silêncio.
    ///
    /// **Não repete um NAK.** Recusa é resposta: o módulo entendeu e não
    /// aceitou, e insistir só atrasaria o diagnóstico. Repete silêncio e
    /// falha de persistência, que são os desfechos em que a repetição tem
    /// chance — e a sonda de baud, barata, roda de novo junto.
    ResultadoConfigGps executa_com_tentativas(Logger& log,
                                              unsigned tentativas = 2);

    /// Baud em que a porta ficou. Interessa a quem for ler depois.
    std::uint32_t baud_final() const { return baud_final_; }

private:
    /// Desfecho de um comando. Silêncio e recusa **não** são a mesma coisa:
    /// um aponta para fiação, energia ou baud errado; o outro diz que o
    /// módulo entendeu e não aceitou. Misturá-los manda procurar no lugar
    /// errado.
    enum class Passo : std::uint8_t { Ok, Nak, Silencio };

    /// `obrigatorio` só muda a severidade do registro: um NAK esperado não
    /// deve entrar no log como ERRO, senão o diagnóstico de verdade some no
    /// meio do ruído.
    Passo comanda(const std::uint8_t* quadro, std::size_t tamanho,
                  std::uint8_t id, const char* nome, Logger& log,
                  bool obrigatorio = true);
    bool espera_ack(std::uint8_t id, Logger& log, bool* recusou,
                    unsigned esperas = kEsperasPorAck);

    /// Uma pergunta curta: escreve o quadro **uma vez** e espera pouco.
    /// Responde se o módulo fala NESTE baud — nada mais.
    Passo sonda(const std::uint8_t* quadro, std::size_t tamanho,
                std::uint8_t id, Logger& log);

    /// Procura o módulo e devolve `true` se o achou. Deixa a porta no baud
    /// em que ele respondeu, ou em `kBaudFabrica` se ninguém respondeu.
    bool acha_o_modulo(const std::uint8_t* quadro, std::size_t tamanho,
                       Logger& log, bool* recusou);

    /// A sequência em si. Separada de `executa` para que o **resumo saia por
    /// um caminho só**: são oito pontos de saída, e cobrar de cada um que
    /// lembrasse de registrar é como o recital começou.
    ResultadoConfigGps aplica(Logger& log);

    /// Escreve a linha única do desfecho, no nível que o desfecho merece.
    void registra_resumo(ResultadoConfigGps r, Logger& log);

    Uart&            uart_;
    Pausa&           pausa_;
    ubx::LeitorUbx   leitor_;
    std::uint32_t    baud_final_ = kBaudFabrica;
    Resumo           resumo_;
};

}  // namespace coruja
