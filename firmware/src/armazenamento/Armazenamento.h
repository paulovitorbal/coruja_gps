#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

class Logger;

enum class ErroCartao {
    Nenhum,
    SemCartaoLegivel,
    ArquivoAusente,   ///< montou, e o arquivo não está lá
    ArquivoGrande,    ///< maior que o buffer oferecido
    FalhaDeLeitura,
    FalhaDeEscrita,
    FalhaDeRenomeacao,  ///< a troca atômica do RF05.2 não completou
};

/// `inline` no cabeçalho, e não no `.cpp`: quem usa o **tipo** de erro
/// precisa poder descrevê-lo sem arrastar junto a implementação do cartão,
/// que só existe no alvo Pico. Sem isto, qualquer lógica portável que
/// mencione um erro de cartão deixa de linkar no host.
inline const char* descreve(ErroCartao erro) {
    switch (erro) {
        case ErroCartao::Nenhum:            return "ok";
        case ErroCartao::SemCartaoLegivel:  return "cartao ausente ou ilegivel";
        case ErroCartao::ArquivoAusente:
            return "arquivo nao encontrado em nenhuma particao";
        case ErroCartao::ArquivoGrande:     return "arquivo maior que o buffer";
        case ErroCartao::FalhaDeLeitura:    return "falha de leitura";
        case ErroCartao::FalhaDeEscrita:    return "falha de escrita";
        case ErroCartao::FalhaDeRenomeacao:
            return "falha ao renomear (troca atomica)";
    }
    return "erro desconhecido";
}

/// Armazenamento de arquivos, visto por quem só precisa ler e gravar.
///
/// É o subconjunto do `CartaoSd` que a lógica de atualização usa — e não a
/// classe inteira. A montagem de volume, a sondagem de partições e o
/// `inicia()` ficam de fora porque são do porte, não da decisão.
///
/// **Existe para que a orquestração do OTA seja verificável sem placa.** O
/// `CartaoSd` concreto depende de FatFs e do SDK do Pico, então tudo que o
/// mencionasse diretamente ficava fora do alvo de host — incluindo a troca
/// atômica e as três retentativas do RF05.2, que são justamente o que mais
/// merece teste e o que menos se consegue provocar na bancada.
class Armazenamento {
public:
    virtual ~Armazenamento() = default;

    virtual ErroCartao le_arquivo(const char* nome, char* destino,
                                  std::size_t capacidade, std::size_t* lidos,
                                  Logger& log) = 0;
    virtual ErroCartao grava_arquivo(const char* nome, const char* conteudo,
                                     std::size_t tamanho, Logger& log) = 0;

    /// Acrescenta ao fim, criando se não existir. É o que o log em cartão
    /// usa: reescrever o arquivo inteiro a cada descarga multiplicaria a
    /// escrita num meio de ciclos finitos.
    virtual ErroCartao acrescenta_arquivo(const char* nome,
                                          const char* conteudo,
                                          std::size_t tamanho,
                                          Logger& log) = 0;

    /// Escrita em fluxo: abre, escreve em pedaços, conclui ou descarta. É
    /// assim porque a base não cabe em RAM (formato_dados.md §1).
    virtual ErroCartao abre_para_escrita(const char* nome, Logger& log) = 0;
    virtual bool escreve(const std::uint8_t* bytes, std::size_t tamanho) = 0;
    virtual ErroCartao conclui_escrita(Logger& log) = 0;
    virtual void descarta_escrita(const char* nome, Logger& log) = 0;

    /// Troca atômica do RF05.2: `temporario` vira `base`, e o `base` anterior
    /// vira `reserva`.
    virtual ErroCartao promove(const char* temporario, const char* base,
                               const char* reserva, Logger& log) = 0;
};

}  // namespace coruja
