#include "display/TextoRolante.h"

namespace coruja {

Rolagem rolagem(int largura_texto, int largura_disponivel,
                std::uint32_t agora_ms) {
    Rolagem r;
    if (largura_texto <= largura_disponivel) {
        r.x = (largura_disponivel - largura_texto) / 2;
        return r;
    }

    // Quanto precisa andar para o fim do texto encostar na borda direita.
    const int curso = largura_texto - largura_disponivel;
    // Arredonda o número de passos para CIMA: com divisão truncada o
    // último trecho do texto nunca apareceria, que é justamente o que a
    // rolagem existe para mostrar.
    const int passos = (curso + kPassoRolagemPx - 1) / kPassoRolagemPx;
    const std::uint32_t tempo_curso =
        static_cast<std::uint32_t>(passos) * kMsPorPassoRolagem;
    const std::uint32_t ciclo = kPausaRolagemMs + tempo_curso +
                                kPausaRolagemMs;
    const std::uint32_t t = agora_ms % ciclo;

    r.rolando = true;
    if (t < kPausaRolagemMs) {
        r.x = 0;                       // parado no comeco, para ler
    } else if (t < kPausaRolagemMs + tempo_curso) {
        const int n = static_cast<int>((t - kPausaRolagemMs) /
                                       kMsPorPassoRolagem);
        // Sem limitador: `n` vai no maximo a `passos - 1`, e
        // `(ceil(curso/passo) - 1) * passo < curso` para qualquer curso.
        // A primeira versao tinha um `min` aqui, e a mutacao mostrou que
        // ele nunca disparava. Quem alcanca `-curso` e a pausa final.
        r.x = -(n * kPassoRolagemPx);
    } else {
        r.x = -curso;                  // parado no fim, para ler o resto
    }
    return r;
}

}  // namespace coruja
