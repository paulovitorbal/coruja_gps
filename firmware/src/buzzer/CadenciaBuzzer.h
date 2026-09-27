#pragma once
#include <cstdint>

#include "nucleo/Zonamento.h"

namespace coruja {

/// Um padrão sonoro do RF03.7: quanto tempo ligado, de quanto em quanto.
struct PadraoSonoro {
    std::uint32_t ligado_ms;
    std::uint32_t periodo_ms;
};

/// Os três padrões da Zona de Perigo, na tabela do RF03.7.
///
/// A faixa 3 é **pulso, não tom contínuo**, e isso foi uma correção
/// deliberada de 2026-09-18: tom constante é o padrão **menos** detectável
/// contra ruído estável, porque o sistema auditivo se adapta a ele em
/// segundos — e o ruído de vento faz exatamente isso. Quem "simplificar" a
/// faixa 3 para ligar e deixar ligado estará piorando o alerta mais grave.
constexpr PadraoSonoro padrao_de(FaixaSonora faixa) {
    switch (faixa) {
        case FaixaSonora::Nenhuma: return {0, 0};   // sem padrao
        case FaixaSonora::Lenta:   return {100, 1000};   // ~1 Hz
        case FaixaSonora::Rapida:  return {100, 350};    // ~2,9 Hz
        case FaixaSonora::Pulso:   return {50, 100};     // 10 Hz
    }
    return {0, 0};
}

/// Gera a cadência do RF03.7 a partir da faixa que o `Zonamento` decidiu.
///
/// Sem hardware e sem relógio próprio: o instante entra por parâmetro, como
/// no resto do núcleo. O `main` pergunta a cada volta se o buzzer deve estar
/// ligado e repassa ao porte — nada aqui bloqueia, porque um `sleep` de
/// 100 ms no meio do laço custaria dois fixes de GPS.
class CadenciaBuzzer {
public:
    /// Informa a faixa vigente.
    ///
    /// **É idempotente de propósito**, e isso não é detalhe: o laço principal
    /// chama isto a cada volta com o veredito do `Zonamento`. Se repetir a
    /// mesma faixa reiniciasse a fase, o instante zero do padrão seria sempre
    /// agora, o bipe nunca terminaria e a faixa 3 viraria o tom contínuo que
    /// o RF03.7 proíbe.
    void define_faixa(FaixaSonora faixa, std::uint32_t agora_ms);

    /// Recalcula e devolve se o buzzer deve estar ligado neste instante.
    bool atualiza(std::uint32_t agora_ms);

    bool ligado() const { return ligado_; }
    FaixaSonora faixa() const { return faixa_; }

    /// Cala imediatamente, sem esperar o fim do ciclo. Para o RF07.
    void silencia();

private:
    FaixaSonora   faixa_ = FaixaSonora::Nenhuma;
    std::uint32_t inicio_ = 0;
    bool          ligado_ = false;
};

}  // namespace coruja
