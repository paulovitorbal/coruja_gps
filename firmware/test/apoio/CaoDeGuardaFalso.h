#pragma once
#include <cstdint>

#include "nucleo/CaoDeGuarda.h"
#include "nucleo/Pausa.h"

namespace coruja::teste {

/// Cão de guarda de mentira: conta as refeições em vez de reiniciar nada.
///
/// **A contagem é o ponto.** O que os testes precisam afirmar não é que o
/// aparelho reinicia -- isso é registrador --, é QUANDO o cão é alimentado e,
/// principalmente, quando ele NÃO é. Um teste que só verificasse "alimentou
/// alguma vez" passaria com uma alimentação por tempo decorrido, que é
/// exatamente o defeito que torna o watchdog um enfeite.
class CaoDeGuardaFalso : public CaoDeGuarda {
public:
    unsigned      alimentacoes = 0;
    unsigned      armadas = 0;
    unsigned      suspensoes = 0;
    unsigned      retomadas = 0;
    std::uint32_t prazo_ms = 0;
    bool          veio_de_travamento = false;

    void arma(std::uint32_t ms) override { ++armadas; prazo_ms = ms; }
    void alimenta() override { ++alimentacoes; }
    void suspende() override { ++suspensoes; }
    void retoma() override { ++retomadas; }
    bool reiniciou_por_travamento() const override {
        return veio_de_travamento;
    }
};

/// O mesmo cão, agora com cronômetro: guarda o maior intervalo de relógio
/// falso entre duas refeições.
///
/// **É o número que decide se o prazo escolhido serve.** Contar refeições diz
/// que alguém alimenta; sem o intervalo, uma operação que alimentasse no
/// começo e no fim e ficasse vinte segundos calada no meio passaria igual --
/// e seria justamente o aparelho reiniciando sozinho no carro.
///
/// Lê a hora da MESMA `Pausa` que a operação sob teste usa, então o tempo que
/// ele mede é o tempo que a operação de fato gastaria.
class CaoCronometrado : public CaoDeGuardaFalso {
public:
    /// O cronometro parte da hora ATUAL do relogio falso, e nao de zero:
    /// alguns relogios de teste comecam longe da origem de proposito, e a
    /// distancia ate a origem nao e silencio de ninguem.
    explicit CaoCronometrado(Pausa& pausa)
        : pausa_(pausa), ultima_ms_(pausa.agora_ms()) {}

    void alimenta() override {
        CaoDeGuardaFalso::alimenta();
        marca();
        ultima_ms_ = pausa_.agora_ms();
    }

    /// ⚠️ **A janela de suspensao NAO conta.** Com o cao solto ele nao pode
    /// morder, e somar esse tempo ao silencio acusaria um defeito que nao
    /// existe -- seria medir a associacao do Wi-Fi, que roda solta de
    /// proposito.
    void suspende() override {
        CaoDeGuardaFalso::suspende();
        marca();
    }
    void retoma() override {
        CaoDeGuardaFalso::retoma();
        ultima_ms_ = pausa_.agora_ms();
    }

    /// Fecha a conta até o fim da operação: o último trecho, entre a última
    /// refeição e o retorno, conta tanto quanto os do meio.
    void fecha() { marca(); }

    std::uint32_t maior_intervalo_ms = 0;

private:
    void marca() {
        const std::uint32_t desde = pausa_.agora_ms() - ultima_ms_;
        if (desde > maior_intervalo_ms) { maior_intervalo_ms = desde; }
    }

    Pausa& pausa_;
    std::uint32_t  ultima_ms_ = 0;
};

}  // namespace coruja::teste
