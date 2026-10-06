#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Leitura da resposta crua do servidor de recepção.
///
/// **Vive separado do cliente de propósito.** O `ClienteEnvio` só compila para
/// o RP2350, porque arrasta o lwIP junto; estas duas funções são a parte da
/// conversa que decide — uma diz se o pedido deu certo, a outra produz o
/// número que autoriza apagar o arquivo do cartão. Enterradas no `.cpp` do
/// cliente, seriam o único trecho do caminho de envio sem teste nenhum.

/// Código da linha de status (`HTTP/1.1 201 Created` → 201).
///
/// Zero quando a resposta não começa com uma linha de status reconhecível —
/// que é o que se recebe de um servidor que não é o esperado, ou de uma
/// conexão que entregou lixo.
std::uint32_t status_da_resposta(const char* texto, std::size_t tamanho);

/// O corpo, depois da linha em branco que separa os cabeçalhos.
///
/// Devolve `nullptr` se os cabeçalhos ainda não terminaram. Aceita `\r\n\r\n`
/// e `\n\n`: o servidor de referência manda o primeiro, mas um proxy no
/// caminho pode normalizar, e recusar por causa disso seria falhar por uma
/// diferença que não muda nada.
const char* corpo_da_resposta(const char* texto, std::size_t tamanho,
                              std::size_t* tamanho_do_corpo);

/// Lê oito dígitos hexadecimais, o CRC-32 que o servidor devolve.
///
/// Exige **exatamente oito**, e é por isso que não usa `strtoul`: "1a2b" seria
/// aceito e viraria 0x1A2B, um número plausível e errado — e um CRC errado que
/// por acaso bate autoriza apagar o único exemplar de um arquivo.
///
/// Espaço em branco em volta é tolerado; qualquer outro caractere recusa.
bool le_crc_hex(const char* texto, std::size_t tamanho, std::uint32_t* destino);

}  // namespace coruja
