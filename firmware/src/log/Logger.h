#pragma once

namespace coruja {

enum class Nivel { Debug, Info, Warning, Error };

/// Interface de log (regra 7). Existe como interface, e nao como funcao livre,
/// para que os testes possam observar o que foi registrado sem escrever em
/// lugar nenhum, e para que o destino (USB-CDC, cartao, nada) seja trocavel
/// sem tocar em quem registra.
class Logger {
public:
    virtual ~Logger() = default;

    /// `origem` identifica o modulo; `mensagem` nao deve conter quebra de linha.
    virtual void registra(Nivel nivel, const char* origem,
                          const char* mensagem) = 0;

    /// Abaixo deste nivel as chamadas sao descartadas.
    virtual void define_nivel_minimo(Nivel nivel) = 0;

    void debug(const char* o, const char* m)   { registra(Nivel::Debug, o, m); }
    void info(const char* o, const char* m)    { registra(Nivel::Info, o, m); }
    void warning(const char* o, const char* m) { registra(Nivel::Warning, o, m); }
    void error(const char* o, const char* m)   { registra(Nivel::Error, o, m); }
};

const char* nome_nivel(Nivel nivel);

}  // namespace coruja
