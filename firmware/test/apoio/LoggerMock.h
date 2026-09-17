#pragma once
#include <string>
#include <vector>

#include "log/Logger.h"

namespace coruja::teste {

/// Mock de `Logger` que guarda o que foi registrado, para os testes afirmarem
/// sobre o log sem escrever em nada (regra 2).
class LoggerMock final : public Logger {
public:
    struct Entrada {
        Nivel       nivel;
        std::string origem;
        std::string mensagem;
    };

    void registra(Nivel nivel, const char* origem,
                  const char* mensagem) override {
        if (nivel < minimo_) {
            return;
        }
        entradas_.push_back({nivel, origem != nullptr ? origem : "",
                             mensagem != nullptr ? mensagem : ""});
    }

    void define_nivel_minimo(Nivel nivel) override { minimo_ = nivel; }

    const std::vector<Entrada>& entradas() const { return entradas_; }

    std::size_t contagem(Nivel nivel) const {
        std::size_t n = 0;
        for (const auto& e : entradas_) {
            if (e.nivel == nivel) {
                ++n;
            }
        }
        return n;
    }

    bool contem(Nivel nivel, const std::string& trecho) const {
        for (const auto& e : entradas_) {
            if (e.nivel == nivel && e.mensagem.find(trecho) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    void limpa() { entradas_.clear(); }

private:
    std::vector<Entrada> entradas_;
    Nivel                minimo_ = Nivel::Debug;
};

}  // namespace coruja::teste
