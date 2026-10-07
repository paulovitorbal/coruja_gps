// A metade da medicao que depende do mapa de memoria do RP2350.
//
// A varredura vive em `MedidorPilha.cpp`, portavel e testada; aqui ficam so
// os simbolos do ligador e o cuidado de nao pintar o proprio quadro.
#include "placa/MedidorPilha.h"

#include <cstring>

extern "C" {
// Postos pelo script de ligacao do pico-sdk (`section_end.incl`):
//
//     __StackTop    = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y)
//     __StackBottom = __StackTop - SIZEOF(.stack_dummy)
//     __StackLimit  = ORIGIN(RAM) + LENGTH(RAM)
//
// ⚠️ **`__StackLimit` NAO e o fundo da pilha** -- e o fim da RAM inteira, a
// 8 KiB daqui, atravessando o SCRATCH_X onde moraria a pilha do nucleo 1. A
// primeira versao disto o usou como fundo, pintou e varreu a area errada, e
// reportou "a pilha foi usada ate o fundo" numa execucao em que ela nao
// tinha sido. O nome enganou; o mapa do binario desmentiu.
extern char __StackBottom;
extern char __StackTop;
}

namespace coruja {
namespace {

/// Quanto deixar sem pintar abaixo do ponteiro atual.
///
/// Pintar ate o ponteiro apagaria o quadro de quem esta chamando -- inclusive
/// o endereco de retorno. 256 bytes cobrem este quadro e o do chamador com
/// folga.
constexpr std::size_t kMargemDoQuadro = 256;

bool pintada_ = false;

std::uint32_t* fundo() {
    return reinterpret_cast<std::uint32_t*>(&__StackBottom);
}

std::size_t total_em_palavras() {
    return static_cast<std::size_t>(&__StackTop - &__StackBottom)
           / sizeof(std::uint32_t);
}

}  // namespace

void pinta_pilha() {
    std::uint32_t* base = fundo();
    // Onde estamos agora, menos a margem.
    std::uint32_t aqui = 0;
    auto* limite = reinterpret_cast<std::uint32_t*>(
        reinterpret_cast<char*>(&aqui) - kMargemDoQuadro);
    if (limite <= base) { return; }

    for (std::uint32_t* p = base; p < limite; ++p) {
        *p = kTintaDaPilha;
    }
    pintada_ = true;
}

bool pico_da_pilha(std::size_t* usado, std::size_t* total) {
    if (!pintada_ || usado == nullptr || total == nullptr) { return false; }
    const std::size_t palavras = total_em_palavras();
    const std::size_t livres = palavras_intocadas(fundo(), palavras);
    if (livres == 0) {
        // A tinta sumiu inteira: ou a pilha foi usada ate o fundo, ou passou
        // dele. Devolver o total como "pico" esconderia exatamente isso.
        return false;
    }
    *total = palavras * sizeof(std::uint32_t);
    *usado = (palavras - livres) * sizeof(std::uint32_t);
    return true;
}

}  // namespace coruja
