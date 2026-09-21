#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

class Logger;

enum class ErroCartao {
    Nenhum,
    Ausente,          ///< o `DET` diz que não há cartão no slot
    NaoMontou,        ///< há cartão, e nenhuma partição FAT legível
    ArquivoAusente,   ///< montou, e o arquivo não está lá
    ArquivoGrande,    ///< maior que o buffer oferecido
    FalhaDeLeitura,
};

const char* descreve(ErroCartao erro);

/// Acesso ao cartão microSD.
///
/// **A presença é lida do pino `DET`, não inferida da falha de montagem.** É o
/// que permite ao RF07 separar *cartão ausente*, que o motorista resolve
/// inserindo o cartão, de *cartão ilegível*, que ele não resolve dirigindo.
/// Sem o `DET` os dois viram a mesma mensagem inútil.
///
/// Polaridade, citada do guia do fabricante para o breakout Adafruit 4682:
///
/// > *"DET — This pin is connected to GND internally when there's no card, but
/// > when one is inserted it is pulled up to 3V with a 4.7 kΩ resistor."*
///
/// Cartão inserido = **ALTO**. Sem cartão = **BAIXO**.
class CartaoSd {
public:
    /// Configura o `DET`. Pode ser chamado no boot; não toca no barramento.
    void inicia(Logger& log);

    /// Lê o `DET` **agora**. Não guarda estado: o cartão pode ser removido
    /// entre duas chamadas, e um valor em cache mentiria.
    bool presente() const;

    /// Nível elétrico cru do `DET`, sem interpretação.
    ///
    /// Existe porque a mensagem de log não pode **afirmar** um nível a partir
    /// do significado: quando a polaridade foi corrigida, um texto que dizia
    /// "(DET em nível baixo)" passou a sair com o pino em alto. Uma mensagem
    /// que mente sobre a medição é pior que uma sem detalhe.
    bool nivel_bruto() const;

    /// Lê o `DET` sob os **três** pulls internos e registra os resultados.
    ///
    /// É o que distingue um pino **flutuante** de um pino **acionado**, e
    /// nenhuma leitura isolada consegue fazer isso:
    ///
    ///   segue o pull (BAIXO/ALTO/?) -> FLUTUANTE, nada o aciona
    ///   ALTO nos três ............. -> acionado em alto (pull-up externo)
    ///   BAIXO nos três ............ -> acionado em baixo (chave ao GND)
    ///
    /// Duas leituras com pulls diferentes já se contradisseram neste projeto,
    /// e cada uma sozinha parecia conclusiva. Ver R-41.
    ///
    /// Chame ANTES de `inicia()`: ele mexe no pull do pino.
    void diagnostica_det(Logger& log);

    /// Monta e lê um arquivo inteiro para `destino`.
    ///
    /// Monta e desmonta a cada chamada, de propósito: o cartão é removível, e
    /// manter um volume montado através de uma remoção é como se corrompe
    /// sistema de arquivos. Ver R-38 sobre a escolha da partição.
    ErroCartao le_arquivo(const char* nome, char* destino, std::size_t capacidade,
                          std::size_t* lidos, Logger& log);

private:
    bool iniciado_ = false;
};

}  // namespace coruja
