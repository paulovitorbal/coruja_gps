#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// O que a linha periódica de saúde relata, já reduzido a valores.
///
/// Existe separado do `main` porque a linha **mudou de conteúdo três vezes**
/// e cada mudança passou sem teste: o `hdop` entrou em 25/10/2026, os dois
/// números da serial em 26/10, e a temperatura em 27/10, quando ela deixou de
/// ter linha própria. Formatar aqui dentro deixa o texto sob suíte; montar a
/// estrutura lá fora é o que o `main` sabe fazer e nenhum alvo de host sabe.
///
/// ⚠️ **Os pares "medi" / "não medi" são dois campos de propósito.** Em
/// 15/10/2026 o diário gravou `agc 0/8191` numa partida em que o módulo não
/// respondeu, e a linha se leu como front-end morto. Zero e silêncio não
/// podem sair iguais — ver `EstadoNavegacao::respondeu`.
struct EstadoVigia {
    std::uint32_t pontos_base = 0;
    bool          tem_fix = false;
    std::uint8_t  zona = 0;

    bool          tem_qualidade = false;  ///< o fix trouxe HDOP e contagem?
    float         hdop = 0.0F;
    std::uint8_t  satelites = 0;

    bool          tem_temperatura = false;  ///< a pastilha respondeu?
    float         temperatura_c = 0.0F;

    /// Bytes que o anel da serial recusou por estar cheio. **Zero é o estado
    /// saudável**; diferente de zero diz, em bytes, o tamanho do atraso do
    /// laço em relação ao receptor.
    std::uint32_t serial_perdidos = 0;
    /// A volta mais demorada do laço na janela, em ms.
    std::uint32_t pior_volta_ms = 0;
};

/// Quanto espaço a linha pede. O `Logger` corta em 192 contando carimbo,
/// identificador de execução e origem; 160 cabem com folga nos dois.
constexpr std::size_t kTamLinhaVigia = 160;

/// Escreve a linha em `destino` e devolve quantos caracteres usou.
///
/// Devolve 0 se não couber — e `destino` fica com texto vazio, nunca com
/// linha pela metade: meia linha num diário é pior que linha nenhuma, porque
/// quem lê não sabe que faltou pedaço.
std::size_t formata_vigia(const EstadoVigia& estado, char* destino,
                          std::size_t capacidade);

}  // namespace coruja
