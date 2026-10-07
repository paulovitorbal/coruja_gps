#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Tamanho de um carimbo, com o terminador. `2026-10-07T14:41:22Z` são 20.
constexpr std::size_t kTamCarimbo = 21;

/// O carimbo de tempo de UMA linha de log.
///
/// Existe porque o log do aparelho não tinha hora nenhuma, e sem hora
/// qualquer ferramenta de análise carimba a linha com o instante em que **ela
/// ingeriu o arquivo** — que pode ser dias depois. A análise temporal vira
/// ficção.
///
/// ⚠️ **O relógio pode não estar acertado, e isso tem de ser VISÍVEL.** O
/// RP2350 não tem bateria no relógio: todo boot começa em zero e só depois do
/// NTP ele sabe a hora. Inventar uma data para esse trecho seria pior que não
/// ter nenhuma — alguém leria 1970 como se fosse medição.
///
/// Por isso são duas formas, e elas não se confundem nem de olho nem por
/// expressão regular:
///
///   - relógio acertado → `2026-10-07T14:41:22Z`, ISO 8601, que toda
///     ferramenta lê sem configuração;
///   - relógio zerado   → `+00012345`, milissegundos desde o boot. O `+` diz
///     "isto é um intervalo, não um instante".
///
/// `utc_s` igual a zero é exatamente o que o `hora_utc()` devolve enquanto
/// ninguém acertou o relógio. Uma data implausível (antes de 2020 ou depois
/// de 2099) cai na mesma forma: ela não veio de medição.
void formata_carimbo(std::int64_t utc_s, std::uint32_t ms_desde_boot,
                     char* destino, std::size_t capacidade);

}  // namespace coruja
