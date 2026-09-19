#pragma once
#include <cstdint>

namespace coruja {

/// Decodifica os dois canais em quadratura do KY-040 em detentes.
///
/// Lógica pura: recebe amostras de `CLK` e `DT` e não conhece GPIO nenhum.
/// É o que permite testar a decodificação inteira no host, incluindo ruído.
///
/// Usa tabela de transição de 16 entradas em vez de comparar bordas. A
/// diferença importa: uma transição impossível — os dois canais mudando na
/// mesma amostra — contribui **zero** em vez de ser contada como meio passo.
///
/// **Esta tabela é o debounce do projeto.** Repique de contato é oscilação
/// entre dois estados adjacentes, e nela isso soma `+1, −1, +1, −1`: zero
/// líquido. Um passo só sai com **quatro transições válidas consecutivas na
/// mesma direção**, que ruído simétrico não produz.
///
/// Não há filtro RC no hardware. A revisão 2 do `bom_schematic.md` exigia
/// 100 nF em cada canal, e medido na placa esse filtro **impedia qualquer
/// decodificação** — τ de 1 ms contra fases de 45 a 128 ms apagava o estado
/// `(0,0)`. O RF04 foi revisado: o RC virou contingência de 1 a 10 nF, a
/// soldar só se o brilho oscilar no veículo. Ver R-36.
///
/// A vantagem de fundo: o filtro em software é **independente de
/// frequência**, e o RC não é.
///
/// Num detente os dois canais ficam em nível alto, e cada detente percorre
/// quatro transições válidas. Daí o passo ser emitido a cada ±4.
class DecodificadorQuadratura {
public:
    /// `invertido` troca o sinal do giro. Depende de como `CLK` e `DT` foram
    /// soldados, e a serigrafia do KY-040 não é confiável — a própria pinagem
    /// da placa saiu invertida da documentação usual. Confirmar na bancada.
    explicit DecodificadorQuadratura(bool invertido = false)
        : invertido_(invertido) {}

    /// Alimenta uma amostra. Devolve `+1` para um detente horário, `-1` para
    /// anti-horário e `0` quando ainda não completou um detente.
    int amostra(bool clk, bool dt);

    void reinicia();

    /// Exposto para teste: quantos quartos de passo estão acumulados.
    int acumulado() const { return acumulado_; }

private:
    static constexpr int kQuartosPorDetente = 4;

    bool invertido_;
    std::uint8_t estado_anterior_ = 0x03;  ///< detente: CLK e DT em alto
    int          acumulado_       = 0;
    bool         primeira_        = true;
};

}  // namespace coruja
