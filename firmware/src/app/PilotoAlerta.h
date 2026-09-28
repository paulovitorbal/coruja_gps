#pragma once
#include <cstddef>
#include <cstdint>

#include "buzzer/Buzzer.h"
#include "buzzer/CadenciaBuzzer.h"
#include "gps/LeitorGps.h"
#include "led/LedRgb.h"
#include "led/PadraoLed.h"
#include "nucleo/Ponto.h"
#include "nucleo/SeletorPeriodo.h"
#include "nucleo/Zonamento.h"

namespace coruja {

/// O laço do produto, em uma volta.
///
/// Lê o GPS, decide a zona, e manda no LED e no buzzer. É a peça que
/// **junta** — não decide nada por conta própria, e é essa a razão de ela
/// caber em poucas linhas: toda a decisão já está no `Zonamento`, no
/// `CadenciaBuzzer` e no `PadraoLed`, cada um testado sozinho.
///
/// Recebe abstrações (`LedRgb`, `Buzzer`) e o leitor, então a volta inteira
/// roda no host. O que se verifica aqui não é a lógica de cada peça, que já
/// tem suíte própria, e sim **a fiação**: que o veredito chega ao LED certo,
/// que o buzzer só soa em Perigo, que perder o fix apaga tudo.
class PilotoAlerta {
public:
    PilotoAlerta(LeitorGps& gps, LedRgb& led, Buzzer& buzzer)
        : gps_(gps), led_(led), buzzer_(buzzer) {}

    /// Aponta para a base carregada. Chamar de novo após recarregar: o
    /// `Zonamento` guarda o alvo por índice, e os índices mudam.
    void define_base(const Ponto* base, std::size_t quantos);

    /// Uma volta do laço.
    void passo(std::uint32_t agora_ms);

    /// Dia ou noite, ja com o `modo_noturno` do arquivo aplicado. Quem
    /// chama passa isto ao `Brilho`; o piloto nao conhece o display.
    PeriodoDoDia periodo() const { return seletor_.periodo(); }
    void define_modo_noturno(ModoNoturno m) { seletor_.define_modo(m); }

    const Veredito& veredito() const { return veredito_; }
    const MaquinaZona& maquina() const { return maquina_; }

private:
    LeitorGps&     gps_;
    LedRgb&        led_;
    Buzzer&        buzzer_;
    MaquinaZona    maquina_;
    SeletorPeriodo seletor_;
    CadenciaBuzzer cadencia_;
    PadraoLed      padrao_;
    Veredito       veredito_;
    const Ponto*   base_ = nullptr;
    std::size_t    quantos_ = 0;
};

}  // namespace coruja
