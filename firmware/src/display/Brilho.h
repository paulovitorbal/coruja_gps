#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/PeriodoDoDia.h"

namespace coruja {

/// Passos de 5% do RF04: 21 posições, de 0% a 100%.
constexpr std::size_t kPassosBrilho = 21;

/// **A escala do usuário vai de 0% a 100%, e 0% NÃO apaga a tela.**
///
/// Zero aqui significa "o mínimo que este painel consegue mostrar", que é
/// `kPisoFisicoPct` de duty. A separação é deliberada: o piso é propriedade
/// do hardware — mudou de 5% para 10% quando o R-05 foi medido, e pode
/// mudar de novo com outro painel — e não há razão para o motorista
/// conhecê-lo. Ele quer "o mais escuro possível", e isso se chama 0%.
///
/// Antes a escala exposta era a física, e o piso vazava para o arquivo de
/// configuração, para o menu e para a tela. Uma remedição do R-05 teria de
/// reescrever os três.
constexpr std::uint8_t kBrilhoMinimoPct = 0;
constexpr std::uint8_t kBrilhoMaximoPct = 100;

/// O duty mínimo real, em percentual de luminância física.
///
/// **Medido no painel em 2026-09-29 (R-05), não escolhido.** A 5% a barra
/// da faixa inferior não se enxerga; a 10%, sim. E não é a cor que falha:
/// as três matizes continuam distinguíveis entre si a 5% — o que some é a
/// barra inteira contra o fundo, por falta de área acesa. Por isso o
/// remédio é brilho, e a paleta segue válida.
constexpr std::uint8_t kPisoFisicoPct = 10;

/// Frequência mínima do PWM do backlight.
///
/// Abaixo de ~1 kHz o painel cintila de forma perceptível na visão
/// periférica e pode produzir efeito estroboscópico com feições da estrada.
/// Entre 1 e 20 kHz alguns módulos assobiam. Daí os 20 kHz.
constexpr std::uint32_t kFrequenciaPwmHz = 20000;

/// O duty do **primeiro** passo da curva, `(5/100)^2,2 x 65535`.
///
/// Exposto porque a porta de PWM precisa conferir que ele nao vira zero na
/// resolucao dela. Uma porta que trunque 90 para 0 apaga a tela no piso do
/// RF04 -- e foi exatamente o que aconteceu com um wrap de 8 bits, onde
/// `90/257` da 0. A promessa de `duty()` ("nunca zero") vale para quem
/// chama, e a porta tem de honra-la.
constexpr std::uint16_t kDutyMinimo = 90;

/// Ajuste de brilho do RF04, com a curva perceptual.
///
/// **A curva não é enfeite.** A percepção humana de brilho é aproximadamente
/// logarítmica: com passos lineares de *duty cycle*, toda a mudança
/// perceptível acontece no fundo da escala e os dez cliques de cima não
/// fazem nada. A tabela abaixo é `(pct/100)^2,2`, gerada fora do código.
class Brilho {
public:
    void aumenta();
    void diminui();

    /// Carrega os dois presets do `coruja.cfg`, em porcentagem.
    ///
    /// Valor fora do passo de 5 e arredondado para o passo mais proximo,
    /// e nao recusado: o leitor de configuracao ja recusa o que vem do
    /// arquivo, entao o que chega aqui vem do menu, onde nao ha como
    /// digitar errado. Fora da faixa vai para a ponta.
    void define_presets(std::uint8_t dia_pct, std::uint8_t noite_pct);

    /// Os presets como vao para o arquivo.
    std::uint8_t pct_dia() const;
    std::uint8_t pct_noite() const;

    /// Informa dia ou noite. Troca o preset vigente; `Desconhecido` **não
    /// muda nada** — sem data o aparelho não sabe, e mexer no brilho por
    /// palpite seria pior que deixar como está.
    void define_periodo(PeriodoDoDia periodo);

    /// O que o usuário vê na barra: 5 a 100.
    std::uint8_t percentual() const;

    /// O que vai ao PWM, de 0 a 65535. **Nunca zero**, mesmo no piso.
    std::uint16_t duty() const;

    /// Posição do passo, 1 a 20. Para a barra de ajuste da faixa superior.
    std::size_t passo() const;

    PeriodoDoDia periodo() const { return periodo_; }

private:
    std::size_t& passo_vigente();
    std::size_t  passo_vigente() const;

    /// **Dois presets, e o ajuste manual edita o vigente.** É o que faz o
    /// aparelho lembrar: acerta-se o brilho uma vez de dia e uma vez de
    /// noite, e a transição seguinte já vem no valor certo. Um preset só
    /// obrigaria a reajustar duas vezes por dia, para sempre.
    std::size_t passo_dia_ = kPassosBrilho - 1;  ///< 100%
    /// 20% é chute de partida, para ajustar na estrada. Sem medição, é o
    /// que se pode dizer honestamente — e o R-05 ainda vai dizer se o piso
    /// de 5% é utilizável.
    std::size_t passo_noite_ = 4;             ///< 20% (passo*5)
    PeriodoDoDia periodo_ = PeriodoDoDia::Desconhecido;
};

}  // namespace coruja
