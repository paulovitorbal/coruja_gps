#pragma once
#include <cstdint>

#include "nucleo/Nmea.h"
#include "nucleo/TempoUtc.h"

namespace coruja {

class Logger;

/// O relógio que sobrevive entre uma consulta e outra, dentro da ligação.
///
/// No RP2350 é o temporizador do bloco POWMAN — o RP2040 tinha um RTC, o
/// RP2350 não tem; o periférico sumiu e o substituto conta segundos.
///
/// ⚠️ **Não tem bateria na placa.** Ele mantém a hora enquanto houver
/// alimentação, e o aparelho desliga com a ignição: todo boot começa do
/// zero. Por isso a sincronização roda por ligação, e não uma vez na vida.
class RelogioPersistente {
public:
    virtual ~RelogioPersistente() = default;

    /// Segundos desde 1970. Zero, ou qualquer coisa implausível, significa
    /// "ninguém acertou a hora nesta ligação".
    virtual std::int64_t agora_utc() const = 0;
    virtual void define_utc(std::int64_t segundos) = 0;
};

/// Quem consegue a hora pela rede.
class FonteNtp {
public:
    virtual ~FonteNtp() = default;

    /// Consulta o servidor e preenche `segundos`. `false` em qualquer falha.
    /// Bloqueia, como o resto do caminho de rede deste firmware.
    virtual bool consulta(const char* servidor, std::int64_t* segundos,
                          Logger& log) = 0;
};

enum class OrigemHora : std::uint8_t {
    Nenhuma,      ///< não se conseguiu hora de lugar nenhum
    JaAjustado,   ///< o relógio desta ligação já estava acertado
    Ntp,
    Gps,
};

const char* descreve(OrigemHora o);

/// Servidor consultado quando a configuração não diz outro.
///
/// `pool.ntp.br` é o pool brasileiro do NIC.br: responde de dentro do país,
/// com latência menor e sem depender de uma rota internacional para o
/// aparelho conseguir validar um certificado.
constexpr const char* kServidorNtpPadrao = "pool.ntp.br";

/// Acerta o relógio antes de o TLS precisar dele.
///
/// **A ordem é NTP primeiro, GPS depois, e isso é deliberado.** O caso de uso
/// manda: atualizar e enviar dados só acontece com Wi-Fi, ou seja, com o carro
/// parado onde há rede — garagem, estacionamento coberto. É exatamente onde o
/// GPS não pega. Tentar o GPS primeiro significaria **esperar um fix falhar**
/// em todas as vezes que o recurso é usado, para só então ir à rede.
///
/// O GPS não é descartado: ele entra se a telemetria **já** trouxer data
/// válida no instante da chamada. Ler o que já chegou não custa espera
/// nenhuma, e cobre o caso de a rede ter NTP bloqueado.
///
/// ⚠️ **NTP simples é UDP sem autenticação.** Dar prioridade a ele significa
/// que quem controla a rede controla o relógio — e, por tabela, consegue
/// fazer um certificado vencido parecer válido. O GPS seria imune a isso,
/// porque a hora dele não passa pelo Wi-Fi. A troca foi feita com
/// conhecimento de causa pelo autor em 2026-10-06: o custo de esperar o fix
/// falhar a cada uso é certo e cotidiano; o ataque exige alguém dentro da
/// rede de onde o carro estaciona.
class SincronizadorHora {
public:
    SincronizadorHora(RelogioPersistente& relogio, FonteNtp& ntp)
        : relogio_(relogio), ntp_(ntp) {}

    /// Acerta o relógio, se ainda não estiver. `gps` é a última telemetria
    /// conhecida — pode estar sem data, e aí é simplesmente ignorada.
    OrigemHora sincroniza(const char* servidor, const Telemetria& gps,
                          Logger& log);

    /// A hora de parede agora, em segundos desde 1970. Zero se ninguém
    /// conseguiu acertar — e aí o TLS vai recusar tudo por data, que é o
    /// lado seguro de não saber.
    std::int64_t hora_utc() const { return relogio_.agora_utc(); }

    /// Força nova consulta mesmo com o relógio já ajustado.
    ///
    /// Existe para quando a validação de certificado falhar por data: pode
    /// ser que a hora tenha sido acertada por uma fonte ruim, e insistir com
    /// a mesma não levaria a lugar nenhum.
    void esquece() { ja_sincronizado_ = false; }

private:
    bool aceita(std::int64_t segundos, OrigemHora origem, Logger& log);

    RelogioPersistente& relogio_;
    FonteNtp&           ntp_;
    bool                ja_sincronizado_ = false;
};

}  // namespace coruja
