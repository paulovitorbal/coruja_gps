#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Liga o mbedTLS ao mundo deste aparelho.
///
/// Registra a função de hora. **Precisa ser chamada antes de qualquer conexão
/// TLS**, e depois de o relógio estar acertado.
///
/// ⚠️ **Não instala alocador, e isso é deliberado.** Houve uma versão que
/// emprestava os 280 KB do vetor de radares ao mbedTLS. Não funcionava: o
/// `altcp_tls_mbedtls_mem.c` do lwIP chama
/// `mbedtls_platform_set_calloc_free()` por conta própria e sobrescreve
/// qualquer alocador instalado aqui. A medida de 2026-10-07 mostrou a arena
/// em `0 B de 287984` com o heap do lwIP esgotado.
///
/// Hoje o mbedTLS usa o `calloc` da libc, sobre o heap do sistema — ~120 KiB
/// entre o fim do `.bss` e o fim da RAM, para os ~30 KiB que ele pede.
void inicia_plataforma_mbedtls();

/// O maior uso do heap do SISTEMA já alcançado, e o total, em bytes.
///
/// É de lá que o mbedTLS tira os certificados interpretados e os buffers de
/// registro. Sem este número, descobrir que ficou curto só aconteceria com um
/// handshake falhando em campo — que foi como descobrimos as duas vezes
/// anteriores.
bool pico_do_heap_do_sistema(std::size_t* usado, std::size_t* total);

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
