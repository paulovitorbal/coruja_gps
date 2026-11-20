#pragma once
#include "nucleo/CaoDeGuarda.h"

namespace coruja {

/// Solta o cão enquanto vive e o devolve na saída de QUALQUER caminho.
///
/// **Existe porque `suspende()` e `retoma()` pareados à mão são uma armadilha
/// de um único erro.** Hoje os dois pares (OTA e remessa) estão certos, sem
/// `return` no meio. Mas um terceiro chamador — ou um `return` adiantado
/// acrescentado depois — que esquecesse o `retoma()` deixaria o aparelho
/// SEM cão até o próximo boot, e nada acusaria: o defeito que o cão existe
/// para pegar passaria a ser justamente o que ele não vê. Aqui a devolução é
/// o destrutor, e não há saída que a esqueça.
///
/// `cao` nulo é "ninguém vigia": a bancada e as suítes sem cão seguem iguais.
class CaoSuspenso {
public:
    explicit CaoSuspenso(CaoDeGuarda* cao) : cao_(cao) {
        if (cao_ != nullptr) { cao_->suspende(); }
    }
    ~CaoSuspenso() {
        if (cao_ != nullptr) { cao_->retoma(); }
    }

    // Um guarda copiado devolveria o cão duas vezes.
    CaoSuspenso(const CaoSuspenso&) = delete;
    CaoSuspenso& operator=(const CaoSuspenso&) = delete;

private:
    CaoDeGuarda* cao_;
};

}  // namespace coruja
