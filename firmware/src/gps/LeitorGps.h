#pragma once
#include <cstddef>
#include <cstdint>

#include "gps/Uart.h"
#include "nucleo/MonitorTaxa.h"
#include "nucleo/Nmea.h"
#include "nucleo/Ubx.h"

namespace coruja {

/// Uma sentença NMEA cabe em 82 bytes por norma. O dobro é folga; acima
/// disso não é sentença, é lixo, e acumular faria o buffer crescer sem fim
/// caso o `\n` nunca chegasse.
constexpr std::size_t kLinhaMaxima = 164;

/// Sem fix válido por este tempo, a telemetria está velha demais para
/// alertar sobre ela. São quatro amostras perdidas a 4 Hz (RF01.4).
constexpr std::uint32_t kFixVelhoMs = 1000;

/// Puxa bytes da UART e os transforma em telemetria.
///
/// **Na mesma linha trafegam texto e binário** — sentenças NMEA e quadros
/// UBX de resposta. Cada byte vai aos dois leitores, e é assim de propósito:
/// tentar separar os fluxos exigiria saber onde um quadro começa antes de
/// tê-lo lido.
///
/// A consequência fica contida num detalhe que importa para o RF01.5: bytes
/// de UBX interpretados como texto produzem "linhas" sem `$`, e essas **não
/// entram na contagem de checksum inválido**. Contá-las poluiria exatamente
/// o número que existe para distinguir "o GPS não está enviando" de "estamos
/// falhando em interpretar" — e faria a configuração do módulo parecer ruído
/// elétrico na linha.
class LeitorGps {
public:
    explicit LeitorGps(Uart& uart) : uart_(uart) {}

    /// Lê o que houver e processa. Chame a cada volta do laço: é ela que
    /// alimenta o monitor de taxa, inclusive com o silêncio.
    void atualiza(std::uint32_t agora_ms);

    /// Há fix válido recente o bastante para alertar?
    bool tem_fix(std::uint32_t agora_ms) const;

    const Telemetria& telemetria() const { return telemetria_; }
    const MonitorTaxa& monitor() const { return monitor_; }
    MonitorTaxa& monitor() { return monitor_; }

    /// Último quadro UBX de interesse, para quem espera um ACK.
    ubx::LeitorUbx& ubx() { return ubx_; }

    std::uint32_t fixes() const { return fixes_; }
    std::uint32_t sem_fix() const { return sem_fix_; }
    std::uint32_t linhas_descartadas() const { return descartadas_; }

    void reinicia();

private:
    void processa_linha(const char* linha, std::size_t tamanho,
                        std::uint32_t agora_ms);

    Uart&          uart_;
    MonitorTaxa    monitor_;
    ubx::LeitorUbx ubx_;
    Telemetria     telemetria_;
    char           linha_[kLinhaMaxima + 1] = {};
    std::size_t    usados_ = 0;
    bool           houve_fix_ = false;
    std::uint32_t  ultimo_fix_ms_ = 0;
    std::uint32_t  fixes_ = 0;
    std::uint32_t  sem_fix_ = 0;
    std::uint32_t  descartadas_ = 0;
};

}  // namespace coruja
