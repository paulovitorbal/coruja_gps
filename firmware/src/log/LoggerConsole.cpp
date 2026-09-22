#include "log/LoggerConsole.h"

namespace coruja {

namespace {

bool igual_sem_caixa(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
        ++a; ++b;
    }
    return *a == '\0' && *b == '\0';
}

}  // namespace

bool nivel_de_texto(const char* texto, Nivel* destino) {
    if (texto == nullptr || destino == nullptr) {
        return false;
    }
    // "warn" além de "warning" porque é o que se digita na prática, e recusar
    // um sinônimo óbvio faz o usuário perder tempo com o arquivo certo.
    if (igual_sem_caixa(texto, "debug"))   { *destino = Nivel::Debug;   return true; }
    if (igual_sem_caixa(texto, "info"))    { *destino = Nivel::Info;    return true; }
    if (igual_sem_caixa(texto, "warning") ||
        igual_sem_caixa(texto, "warn"))    { *destino = Nivel::Warning; return true; }
    if (igual_sem_caixa(texto, "error"))   { *destino = Nivel::Error;   return true; }
    return false;
}

const char* nome_nivel(Nivel nivel) {
    switch (nivel) {
        case Nivel::Debug:   return "DEBUG";
        case Nivel::Info:    return "INFO";
        case Nivel::Warning: return "WARN";
        case Nivel::Error:   return "ERRO";
    }
    return "?";
}

LoggerConsole::LoggerConsole(std::FILE* saida, Nivel minimo)
    : saida_(saida != nullptr ? saida : stdout), minimo_(minimo) {}

void LoggerConsole::define_nivel_minimo(Nivel nivel) { minimo_ = nivel; }

void LoggerConsole::registra(Nivel nivel, const char* origem,
                             const char* mensagem) {
    if (nivel < minimo_) {
        return;
    }
    std::fprintf(saida_, "[%-5s] %s: %s\n", nome_nivel(nivel),
                 origem != nullptr ? origem : "?",
                 mensagem != nullptr ? mensagem : "");
    std::fflush(saida_);
}

}  // namespace coruja
