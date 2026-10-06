#pragma once
#include <cstdint>

namespace coruja {

class ArenaMemoria;

/// Liga o mbedTLS ao mundo deste aparelho.
///
/// Registra a função de hora e o alocador. **Precisa ser chamada antes de
/// qualquer conexão TLS**, e depois de o relógio estar acertado.
///
/// `arena` é a memória emprestada da base de radares (ver `EmprestimoDaBase`):
/// o mbedTLS passa a alocar lá dentro em vez de na pilha de sistema, que não
/// tem os 16 KiB do buffer de registro para dar. Passar nulo devolve o
/// alocador padrão — útil para o modo de bancada, inútil em campo.
void inicia_plataforma_mbedtls(ArenaMemoria* arena);

/// Desliga o alocador de arena, antes de a memória voltar para a base.
///
/// Sem isto, uma alocação tardia do mbedTLS escreveria dentro do vetor de
/// radares já recarregado — e o defeito apareceria como um radar em
/// coordenada absurda, muito longe da causa.
void encerra_plataforma_mbedtls();

/// Informa a hora de parede que vai julgar o prazo dos certificados.
///
/// Zero significa "não se sabe". O mbedTLS então enxerga 1970 e recusa todo
/// certificado por "ainda não vale" — que é o lado seguro de não saber a
/// data, e o `SincronizadorHora` já terá dito no log que ninguém acertou o
/// relógio.
void define_hora_utc(std::int64_t segundos);

/// A hora de parede agora, já andada pelo monotônico desde o acerto.
std::int64_t hora_utc();

}  // namespace coruja
