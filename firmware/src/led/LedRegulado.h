#pragma once
#include <cstdint>

#include "led/LedRgb.h"
#include "nucleo/PeriodoDoDia.h"

namespace coruja {

/// A escala de intensidade do LED, em porcentagem.
///
/// **O piso é 5%, e não zero**: zero é o que o interruptor faz, e ter duas
/// maneiras de apagar o LED deixaria o menu com dois controles disputando o
/// mesmo resultado.
constexpr std::uint8_t kIntensidadeLedMinPct = 5;
constexpr std::uint8_t kIntensidadeLedMaxPct = 100;

/// Um `LedRgb` com interruptor e intensidade.
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
///
/// ## 🔴 A escala é LINEAR, e isso é decisão, não esquecimento
///
/// O `Brilho` da tela usa curva perceptual `(pct/100)^2,2`, porque a
/// percepção humana é logarítmica e passos lineares concentram toda a
/// mudança no fundo da escala. Aqui **não**, e o motivo são as cores
/// calibradas.
///
/// Um matiz composto é uma RAZÃO entre canais de 8 bits: o âmbar é
/// `{255, 50, 0}`, razão 0,196. Escalar por uma curva perceptual leva o
/// canal secundário a valores de um dígito, onde a quantização destrói a
/// razão — a 20% da curva o âmbar vira `{7, 1, 0}`, razão 0,143, e o que se
/// vê é vermelho alaranjado. A calibração do R-05 existe justamente para
/// esses matizes não mentirem.
///
/// Com escala linear e piso de 5%, o pior caso é `{13, 3, 0}` — razão 0,23
/// contra 0,196 — e o âmbar continua âmbar. O custo é um controle que parece
/// grosso perto do topo, o que é aceitável para um ajuste de duas posições
/// na prática: claro de dia, fraco de noite.
class LedRegulado final : public LedRgb {
public:
    explicit LedRegulado(LedRgb& alvo) : alvo_(alvo) {}

    /// Presets por período, como os do `Brilho`. Fora da faixa vai à ponta.
    void define_presets(std::uint8_t dia_pct, std::uint8_t noite_pct) {
        dia_ = limita(dia_pct);
        noite_ = limita(noite_pct);
        reaplica();
    }

    /// Dia ou noite. `Desconhecido` **não muda nada** — é a mesma regra do
    /// `Brilho`, e tem de ser: sem ela, perder o fix clarearia o LED
    /// enquanto a tela continuasse escura.
    void define_periodo(PeriodoDoDia periodo) {
        if (periodo == PeriodoDoDia::Desconhecido) { return; }
        const bool noite = periodo == PeriodoDoDia::Noite;
        if (noite == e_noite_) { return; }
        e_noite_ = noite;
        reaplica();
    }

    /// A intensidade vigente, em porcentagem.
    std::uint8_t percentual() const { return e_noite_ ? noite_ : dia_; }

    /// Liga ou desliga. Idempotente: chamar com o valor vigente não toca no
    /// hardware, e é por isso que a aplicação pode chamar a cada volta do
    /// laço sem pensar.
    void liga(bool ligado) {
        if (ligado == ligado_) { return; }
        ligado_ = ligado;
        reaplica();
    }

    bool ligado() const { return ligado_; }

    void define_cor(const Cor& cor) override {
        desejada_ = cor;
        if (ligado_) { alvo_.define_cor(atenua(cor)); }
    }

    /// O que está ACESO, e não o que foi pedido.
    ///
    /// ⚠️ Devolver a cor desejada faria o LED desligado mentir para quem
    /// consulta — e quem consulta, na prévia do host, desenha o LED na tela.
    Cor cor_atual() const override { return alvo_.cor_atual(); }

    /// A última cor pedida, acesa ou não. Para teste e diagnóstico.
    Cor cor_desejada() const { return desejada_; }

private:
    static std::uint8_t limita(std::uint8_t pct) {
        if (pct < kIntensidadeLedMinPct) { return kIntensidadeLedMinPct; }
        if (pct > kIntensidadeLedMaxPct) { return kIntensidadeLedMaxPct; }
        return pct;
    }

    /// Escala os três canais pelo MESMO fator: é o que preserva o matiz.
    ///
    /// ⚠️ **Não há piso por canal, e a mutação é quem diz que não precisa.**
    /// A primeira versão forçava a 1 todo canal que fosse aceso, para o verde
    /// do âmbar não sumir antes do vermelho. Nenhum teste morreu quando esse
    /// piso saiu: o menor canal secundário da paleta é 40, e a 5% o
    /// arredondamento já o leva a 2. O piso protegia um caso que nenhuma cor
    /// do projeto produz.
    ///
    /// O que ficou no lugar dele é um **teste sobre a paleta inteira**: no
    /// piso de intensidade, nenhuma cor calibrada perde canal. Uma cor nova
    /// com canal muito pequeno reprova ali, e aí quem a acrescentar decide —
    /// em vez de ganhar de graça um piso que mente sobre a razão.
    Cor atenua(const Cor& c) const {
        const unsigned pct = percentual();
        return Cor{escala(c.r, pct), escala(c.g, pct), escala(c.b, pct)};
    }

    static std::uint8_t escala(std::uint8_t canal, unsigned pct) {
        if (canal == 0) { return 0; }
        // +50 arredonda em vez de truncar.
        const unsigned v = (static_cast<unsigned>(canal) * pct + 50U) / 100U;
        return static_cast<std::uint8_t>(v);
    }

    void reaplica() {
        alvo_.define_cor(ligado_ ? atenua(desejada_) : cores::kApagado);
    }

    LedRgb& alvo_;
    Cor     desejada_ = cores::kApagado;
    bool    ligado_ = true;
    std::uint8_t dia_ = kIntensidadeLedMaxPct;
    /// 20% é o palpite de partida, para acertar na estrada — mesma escolha
    /// que o `brilho_noite` fez.
    std::uint8_t noite_ = 20;
    bool    e_noite_ = false;
};

}  // namespace coruja
