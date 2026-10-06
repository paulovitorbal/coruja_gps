#pragma once
#include <cstdint>

namespace coruja {

/// Um instante em UTC, como o GPS e o NTP o entregam.
///
/// Sem fuso: a conversão para o horário local é de quem exibe, e o fuso é
/// constante de compilação (ADR 0002).
struct TempoUtc {
    std::uint16_t ano = 0;
    std::uint8_t  mes = 0, dia = 0;
    std::uint8_t  hora = 0, minuto = 0, segundo = 0;

    /// A data está na faixa que este aparelho pode ter vivido?
    ///
    /// **Não é validação de calendário, é detecção de relógio não ajustado.**
    /// O RP2350 não tem bateria no relógio: todo boot começa em zero, e zero
    /// vira 1970. A pergunta que isto responde é "alguém já acertou a hora
    /// nesta ligação", e não "31 de fevereiro existe".
    ///
    /// ⚠️ Com TLS, um relógio plausível mas **errado** aceita certificado
    /// vencido ou recusa um válido. Plausível não é correto.
    bool plausivel() const;
};

/// Dias desde 1970-01-01, pelo algoritmo `days_from_civil` de Howard Hinnant.
///
/// Adotado em vez de escrito: contar bissexto e virada de século à mão é erro
/// clássico, e este algoritmo é verificável contra qualquer biblioteca de
/// data. Vale para o calendário gregoriano proléptico inteiro.
std::int32_t dias_desde_epoca(int ano, unsigned mes, unsigned dia);

/// O inverso, `civil_from_days` do mesmo autor.
///
/// Existe porque o relógio do RP2350 guarda **segundos**, não calendário: as
/// funções `aon_timer_*_calendar()` do SDK fazem a conversão arrastando o
/// `mktime` da libc junto, que incha o binário sem dar nada que estas trinta
/// linhas não deem — e que, ao contrário delas, não roda na suíte de host.
void civil_de_dias(std::int32_t dias, int* ano, unsigned* mes, unsigned* dia);

/// Segundos desde 1970-01-01T00:00:00Z.
///
/// 64 bits e não 32: em 2038 um `int32` estoura, e o aparelho que estiver no
/// painel de alguém naquele dia não deve parar de validar certificado por
/// causa de um tipo. Não está em caminho crítico — roda uma vez por boot.
std::int64_t segundos_de(const TempoUtc& t);

/// O caminho de volta. Fora da faixa representável devolve um `TempoUtc`
/// zerado, que o `plausivel()` recusa.
TempoUtc de_segundos(std::int64_t segundos);

}  // namespace coruja
