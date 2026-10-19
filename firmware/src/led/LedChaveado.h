#pragma once

#include "led/LedRgb.h"

namespace coruja {

/// Um `LedRgb` com interruptor: quando desligado, nada acende.
///
/// **Existe por causa do relato de 19/10/2026**: à noite o LED de 10 mm
/// difuso ilumina a cabine inteira e incomoda mais do que informa. O
/// desligamento é do usuário, no menu.
///
/// ## Por que um decorador, e não uma bandeira no `PilotoAlerta`
///
/// O LED tem **quatro** donos — o alerta, o padrão do OTA, o da remessa e o
/// teste de alertas —, e cada um chama `define_cor` por conta própria. Uma
/// bandeira no alerta deixaria os outros três acendendo um LED que o usuário
/// desligou, e ninguém notaria até acontecer no carro.
///
/// Embrulhado aqui, o interruptor fica num ponto só e todo caminho passa por
/// ele — inclusive os que ainda não existem. É a mesma lição do brilho, que
/// mudava o número e não a tela porque a aplicação dependia de alguém lembrar
/// de chamar o PWM.
///
/// ## A cor desejada continua sendo guardada
///
/// Com o LED desligado o decorador **lembra** a cor que lhe pediram e não a
/// repassa. Religar reacende na cor certa, sem esperar o próximo alerta — e,
/// num trecho sem radar, esse próximo alerta pode demorar quilômetros.
class LedChaveado final : public LedRgb {
public:
    explicit LedChaveado(LedRgb& alvo) : alvo_(alvo) {}

    /// Liga ou desliga. Idempotente: chamar com o valor vigente não toca no
    /// hardware, e é por isso que a aplicação pode chamar a cada volta do
    /// laço sem pensar.
    void liga(bool ligado) {
        if (ligado == ligado_) { return; }
        ligado_ = ligado;
        alvo_.define_cor(ligado_ ? desejada_ : cores::kApagado);
    }

    bool ligado() const { return ligado_; }

    void define_cor(const Cor& cor) override {
        desejada_ = cor;
        if (ligado_) { alvo_.define_cor(cor); }
    }

    /// O que está ACESO, e não o que foi pedido.
    ///
    /// ⚠️ Devolver a cor desejada faria o LED desligado mentir para quem
    /// consulta — e quem consulta, na prévia do host, desenha o LED na tela.
    Cor cor_atual() const override { return alvo_.cor_atual(); }

    /// A última cor pedida, acesa ou não. Para teste e diagnóstico.
    Cor cor_desejada() const { return desejada_; }

private:
    LedRgb& alvo_;
    Cor     desejada_ = cores::kApagado;
    bool    ligado_ = true;
};

}  // namespace coruja
