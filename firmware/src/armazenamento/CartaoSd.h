#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

class Logger;

enum class ErroCartao {
    Nenhum,
    /// Não há cartão, ou ele está ilegível. **Um caso só, de propósito**: por
    /// decisão do autor em 2026-09-22, o projeto não distingue os dois — a
    /// reação é a mesma, e o pino de card detect que fazia a distinção custava
    /// um GPIO, um fio e uma chave de soquete que não abria direito. Ver
    /// `docs/adr/0010`.
    SemCartaoLegivel,
    ArquivoAusente,   ///< montou, e o arquivo não está lá
    ArquivoGrande,    ///< maior que o buffer oferecido
    FalhaDeLeitura,
};

const char* descreve(ErroCartao erro);

/// Acesso ao cartão microSD.
///
/// **A presença não é detectada por pino dedicado.** O card detect foi
/// removido em 2026-09-22 (ADR 0010): o projeto reage igual a cartão ausente e
/// a cartão ilegível, então a distinção não pagava o GPIO, o fio, e uma chave
/// de soquete que se mostrou não confiável — ela não abria por completo, e
/// deixava o pino em 1,13 V, na zona indeterminada da lógica de 3,3 V.
///
/// A presença passa a ser **inferida da montagem**: se o cartão monta, existe.
class CartaoSd {
public:
    /// Prepara o driver e o barramento SPI. Pode ser chamado no boot.
    void inicia(Logger& log);

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
