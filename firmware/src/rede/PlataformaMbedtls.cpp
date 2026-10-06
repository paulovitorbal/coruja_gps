// O que o mbedTLS precisa do mundo e que não existe sem sistema operacional:
// hora de parede, hora monotônica e entropia.
//
// Vive separado do cliente porque não é decisão nenhuma — é adaptação. E vive
// em C++ com ligação C porque é o mbedTLS que chama, não nós.
#include "rede/PlataformaMbedtls.h"

#include <pico/rand.h>
#include <pico/time.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <mbedtls/platform.h>
#include <mbedtls/platform_time.h>

#include "nucleo/ArenaMemoria.h"

namespace coruja {

/// A hora de parede que o mbedTLS vai usar para julgar o prazo do
/// certificado, em segundos desde 1970.
///
/// Mora numa variável e não numa chamada ao relógio porque o `mbedtls_time`
/// tem assinatura de C e não tem por onde receber contexto. Quem a preenche é
/// o `define_hora_utc`, chamado depois que o `SincronizadorHora` acerta o
/// relógio e **antes** de qualquer conexão.
///
/// ⚠️ Zero significa "não se sabe a hora". O mbedTLS então enxerga 1970, e
/// todo certificado válido vira "ainda não começou a valer". Isso é
/// deliberado: recusar por não saber a data é o lado seguro, e o log do
/// `SincronizadorHora` já terá dito que ninguém acertou o relógio.
namespace {
volatile std::int64_t g_hora_utc = 0;
volatile std::uint32_t g_hora_ms_do_boot = 0;
}  // namespace

void define_hora_utc(std::int64_t segundos) {
    g_hora_ms_do_boot = to_ms_since_boot(get_absolute_time());
    g_hora_utc = segundos;
}

std::int64_t hora_utc() {
    if (g_hora_utc == 0) { return 0; }
    // Anda com o relogio monotonico desde o acerto. Sem isto, uma sessao
    // longa usaria a hora do momento do acerto -- e um certificado que
    // vencesse no meio dela continuaria passando.
    const std::uint32_t agora = to_ms_since_boot(get_absolute_time());
    const std::uint32_t decorrido = agora - g_hora_ms_do_boot;
    return g_hora_utc + decorrido / 1000;
}

}  // namespace coruja

namespace coruja {
namespace {

/// A arena emprestada, enquanto durar. Ponteiro global pelo mesmo motivo da
/// hora: o mbedTLS instala funcoes de C sem contexto.
ArenaMemoria* g_arena = nullptr;

mbedtls_time_t hora_para_mbedtls(mbedtls_time_t* destino) {
    const auto agora = static_cast<mbedtls_time_t>(hora_utc());
    if (destino != nullptr) { *destino = agora; }
    return agora;
}

void* aloca_na_arena(std::size_t quantos, std::size_t tamanho) {
    return g_arena != nullptr ? g_arena->aloca(quantos, tamanho) : nullptr;
}

void libera_na_arena(void* p) {
    if (g_arena != nullptr) { g_arena->libera(p); }
}

}  // namespace

void inicia_plataforma_mbedtls(ArenaMemoria* arena) {
    mbedtls_platform_set_time(hora_para_mbedtls);
    g_arena = arena;
    if (arena != nullptr) {
        mbedtls_platform_set_calloc_free(aloca_na_arena, libera_na_arena);
    }
}

void encerra_plataforma_mbedtls() {
    // Solta o ponteiro ANTES de a memoria voltar para a base: uma alocacao
    // tardia escreveria dentro do vetor de radares ja recarregado, e o
    // defeito apareceria como um radar em coordenada absurda -- muito longe
    // da causa.
    g_arena = nullptr;
}

}  // namespace coruja

extern "C" {

mbedtls_ms_time_t mbedtls_ms_time(void) {
    // Monotonico, e nao hora de parede: o mbedTLS usa isto para contar
    // tempo decorrido. Acertar o relogio no meio de uma sessao nao pode
    // fazer um temporizador saltar.
    return static_cast<mbedtls_ms_time_t>(
        to_us_since_boot(get_absolute_time()) / 1000);
}

// A ENTROPIA vem do `pico_mbedtls` do proprio SDK, e nao daqui.
//
// Ele ja define `mbedtls_hardware_poll` sobre `get_rand_64()`, que no RP2350
// e o TRNG por hardware -- nao o oscilador em anel do RP2040. Duas definicoes
// do mesmo simbolo nao linkam, e reimplementar para ter o mesmo resultado
// seria divergir do SDK de graca.
//
// ⚠️ Anotado ao ler a implementacao dele: o laco usa
// `MIN(len, sizeof(rand_data))` onde o certo seria `MIN(len - *olen, ...)`.
// Com `len` que nao seja multiplo de 8, ele escreve ate 7 bytes alem do
// buffer. Nao e alcancavel hoje -- o mbedTLS pede 32 ou 128 bytes, ambos
// multiplos de 8 -- mas e um estouro latente em codigo de terceiro, e saber
// disso e melhor do que descobrir depois.

}  // extern "C"
