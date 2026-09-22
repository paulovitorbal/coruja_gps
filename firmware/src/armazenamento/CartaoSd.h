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
    FalhaDeEscrita,
    FalhaDeRenomeacao,  ///< a troca atômica do RF05.2 não completou
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

    /// Chamada a cada pedaço lido. Não pode bloquear por muito tempo.
    using AoLerPedaco = void (*)(void* contexto, const std::uint8_t* bytes,
                                 std::size_t tamanho);

    /// Lê um arquivo **em fluxo**, entregando pedaços a quem chamou.
    ///
    /// É como a base é carregada no boot: o `radares.bin` tem 214 KB e o vetor
    /// decodificado ocupa 281 KB, então não existe momento em que os dois
    /// caibam juntos. Os bytes viram `Ponto` na passagem.
    ErroCartao le_em_fluxo(const char* nome, AoLerPedaco ao_ler, void* contexto,
                           std::size_t* lidos, Logger& log);

    /// Acrescenta bytes ao fim de um arquivo, criando-o se não existir.
    ///
    /// Usado pelo log. Abre e fecha a cada chamada de propósito: o cartão é
    /// removível, e manter um arquivo de log aberto indefinidamente é como se
    /// perde um sistema de arquivos ao puxar o cartão.
    ErroCartao acrescenta_arquivo(const char* nome, const char* conteudo,
                                  std::size_t tamanho, Logger& log);

    /// Grava um arquivo pequeno inteiro, de uma vez. Para a linha de versão.
    ErroCartao grava_arquivo(const char* nome, const char* conteudo,
                             std::size_t tamanho, Logger& log);

    // -----------------------------------------------------------------------
    // Escrita em fluxo — o download não cabe em RAM
    // -----------------------------------------------------------------------
    //
    // O `radares.bin` tem 214 KB e sobram ~185 KiB. Não há como baixar inteiro
    // e depois gravar: os bytes vão para o cartão **enquanto chegam**, no mesmo
    // callback que alimenta o verificador de CRC.
    //
    // O volume é o mesmo onde o `coruja.cfg` foi encontrado — não se procura de
    // novo, porque a base tem de acompanhar a configuração que a definiu.

    /// Monta e abre `nome` para escrita, truncando. Deixa o volume montado.
    ErroCartao abre_para_escrita(const char* nome, Logger& log);

    /// Acrescenta bytes ao arquivo aberto. `false` em qualquer falha — quem
    /// chama deve parar de alimentar e abortar.
    bool escreve(const std::uint8_t* bytes, std::size_t tamanho);

    /// Fecha o arquivo e desmonta. Use quando o conteúdo foi aceito.
    ErroCartao conclui_escrita(Logger& log);

    /// Fecha, **apaga** o arquivo e desmonta. Use quando o conteúdo foi
    /// recusado: melhor não deixar um `.tmp` meio escrito no cartão.
    void descarta_escrita(const char* nome, Logger& log);

    /// A troca atômica do RF05.2, passos 3 e 4: a base vigente vira `.bak` e o
    /// temporário vira a base. Monta e desmonta por conta própria.
    ///
    /// A ordem importa. Renomear é a operação mais barata e mais atômica que o
    /// FAT oferece — nenhum byte de dado se move —, então a janela em que o
    /// cartão está inconsistente é de uma atualização de diretório.
    ErroCartao promove(const char* temporario, const char* base,
                       const char* reserva, Logger& log);

    bool existe(const char* nome, Logger& log);

private:
    /// Monta o volume de trabalho e devolve seu prefixo em `raiz`.
    ErroCartao monta_volume(char* raiz, std::size_t tam_raiz, Logger& log);

    bool iniciado_ = false;
    /// Volume onde o `coruja.cfg` foi achado. -1 enquanto não se sabe; aí a
    /// escrita sonda como a leitura faz.
    int  volume_ativo_ = -1;
    bool escrevendo_ = false;
    char raiz_aberta_[4] = {};
};

}  // namespace coruja
