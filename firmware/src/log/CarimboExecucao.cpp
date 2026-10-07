#include "log/CarimboExecucao.h"

namespace coruja {
namespace {

FonteDeCarimbo g_fonte = nullptr;

}  // namespace

void define_fonte_de_carimbo(FonteDeCarimbo fonte) { g_fonte = fonte; }

const char* carimbo_agora() {
    if (g_fonte == nullptr) { return ""; }
    const char* c = g_fonte();
    return c != nullptr ? c : "";
}

const char* separador_carimbo() {
    return carimbo_agora()[0] != '\0' ? " " : "";
}

void esquece_fonte_de_carimbo() { g_fonte = nullptr; }

}  // namespace coruja
