#include "log/LoggerConsole.h"

namespace coruja {

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
