#include "log/IdExecucao.h"

namespace coruja {
namespace {

/// 32 caracteres: cinco bits por posicao, e o sorteio de 64 bits cobre os
/// oito com folga.
constexpr std::uint64_t kQuantosNoAlfabeto = sizeof kAlfabetoId - 1;

char g_id[kTamIdExecucao + 1] = {};

}  // namespace

void monta_id_execucao(std::uint64_t sorteio, char* destino) {
    if (destino == nullptr) { return; }
    for (std::size_t i = 0; i < kTamIdExecucao; ++i) {
        destino[i] = kAlfabetoId[sorteio % kQuantosNoAlfabeto];
        sorteio /= kQuantosNoAlfabeto;
    }
    destino[kTamIdExecucao] = '\0';
}

const char* id_execucao() { return g_id; }

const char* separador_execucao() { return g_id[0] != '\0' ? " " : ""; }

void define_id_execucao(std::uint64_t sorteio) {
    monta_id_execucao(sorteio, g_id);
}

void esquece_id_execucao() { g_id[0] = '\0'; }

}  // namespace coruja
