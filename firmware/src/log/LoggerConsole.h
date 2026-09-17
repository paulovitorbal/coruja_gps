#pragma once
#include <cstdio>

#include "log/Logger.h"

namespace coruja {

/// Escreve em `stdout`. No Pico isso e o USB-CDC (`pico_enable_stdio_usb`);
/// no host, o terminal — o mesmo codigo serve aos dois, e e por isso que esta
/// implementacao fica no alvo portavel e nao em `coruja_portes`.
class LoggerConsole final : public Logger {
public:
    explicit LoggerConsole(std::FILE* saida = nullptr,
                           Nivel minimo = Nivel::Info);

    void registra(Nivel nivel, const char* origem,
                  const char* mensagem) override;
    void define_nivel_minimo(Nivel nivel) override;

private:
    std::FILE* saida_;
    Nivel      minimo_;
};

}  // namespace coruja
