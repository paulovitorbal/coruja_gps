#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Padrão com que a pilha livre é pintada no boot.
///
/// Não é zero nem 0xFF: os dois aparecem naturalmente em dado de verdade, e
/// contá-los como "nunca usado" subestimaria o consumo. Este valor não
/// significa nada para ninguém, que é o ponto.
constexpr std::uint32_t kTintaDaPilha = 0xC0FFEE11U;

/// Quantas palavras do começo ainda estão com a tinta.
///
/// É a parte **pura** da medição, e por isso vive separada: varrer memória
/// procurando o primeiro valor diferente é onde o erro mora (sentido da
/// varredura, limite, alinhamento), e é o que roda no host.
///
/// A pilha cresce **para baixo**, então a área nunca tocada é a do começo do
/// vetor. A primeira palavra diferente da tinta marca o ponto mais fundo que
/// a execução já alcançou.
std::size_t palavras_intocadas(const std::uint32_t* base, std::size_t quantas);

/// Pinta a pilha livre. Chamado uma vez, no boot, antes de qualquer trabalho.
///
/// ⚠️ Pinta de `__StackLimit` até uma margem abaixo do ponteiro atual —
/// pintar até o ponteiro apagaria o quadro de quem está chamando.
void pinta_pilha();

/// O maior uso de pilha já alcançado, em bytes, e o total reservado.
///
/// Devolve `false` se a pilha não foi pintada, ou se a tinta não aparece em
/// lugar nenhum — o que significa que ela foi usada **inteira** e o número
/// real é desconhecido. Reportar o total como se fosse o pico, nesse caso,
/// esconderia justamente o transbordo que se quer descobrir.
bool pico_da_pilha(std::size_t* usado, std::size_t* total);

}  // namespace coruja
