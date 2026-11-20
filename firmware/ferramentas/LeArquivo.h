#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace coruja::host {

/// Lê um arquivo inteiro para a memória. `false` com o motivo em `erro`.
///
/// ⚠️ **Existe porque as três ferramentas faziam isto à mão, e as três
/// erravam igual.** O padrão era `fseek` ao fim, `ftell`, e o resultado
/// direto num `std::vector`:
///
///     const long tam = std::ftell(f);
///     std::vector<std::uint8_t> bruto(static_cast<std::size_t>(tam));
///
/// Com um diretório no lugar do arquivo — `confere_viagem viagem.log .`, ou
/// um `--base` apontado para a pasta errada —, o `ftell` devolve −1, o
/// `static_cast` o transforma em 18 exabytes e o programa morre no
/// alocador, sem dizer qual argumento estava errado.
///
/// Aqui o tamanho é conferido antes de virar tamanho de buffer, e o que não
/// é arquivo comum é recusado pelo nome.
bool le_arquivo_inteiro(const char* caminho, std::vector<std::uint8_t>* destino,
                        std::string* erro);

}  // namespace coruja::host
