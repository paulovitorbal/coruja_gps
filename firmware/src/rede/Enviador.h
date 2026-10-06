#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/Url.h"
#include "rede/Baixador.h"

namespace coruja {

class Logger;

// `ErroHttp`, `ResultadoHttp` e `descreve()` vivem em `Baixador.h`.

/// Quem manda bytes para uma URL, e quem pergunta o que chegou lá.
///
/// Irmão do `Baixador`, e separado dele pelo mesmo motivo que `Arquivario` é
/// separado de `Armazenamento`: quem só baixa não precisa saber enviar, e os
/// dublês de teste do OTA não deviam crescer dois métodos que não usam.
///
/// **O corpo é puxado, não empurrado.** A pilha pede um pedaço quando a janela
/// TCP abre; quem implementa `FonteDeBytes` serve. O contrário — entregar o
/// arquivo inteiro e deixar a pilha copiar — exigiria o arquivo em RAM, que é
/// o que não há.
class Enviador {
public:
    /// Pedido de mais corpo. Escreve até `capacidade` bytes em `destino` e
    /// devolve quantos escreveu. **Zero significa fim**, e a pilha para de
    /// pedir; um valor menor que `capacidade` não significa fim, só que o
    /// pedaço veio curto.
    using FonteDeBytes = std::size_t (*)(void* contexto, std::uint8_t* destino,
                                         std::size_t capacidade);

    virtual ~Enviador() = default;

    /// PUT de `tamanho` bytes, com o segredo em `X-Coruja-Token`.
    ///
    /// `tamanho` é o `Content-Length` e é obrigatório: sem ele a alternativa
    /// seria `Transfer-Encoding: chunked`, que obriga o servidor de referência
    /// a remontar os pedaços para só então saber se o arquivo cabe no teto —
    /// ou seja, a ler primeiro e decidir depois, que é o que o teto existe
    /// para evitar.
    ///
    /// ⚠️ `token` **não pode aparecer em log nenhum**, nem em mensagem de
    /// erro. Quem implementa é responsável por isso.
    virtual ResultadoHttp envia(const Url& url, const char* token,
                                FonteDeBytes fonte, void* contexto,
                                std::size_t tamanho, Logger& log,
                                std::uint32_t tempo_limite_ms) = 0;

    /// GET na mesma URL: devolve em `crc` o CRC-32 do que o servidor guardou.
    ///
    /// É esta resposta que autoriza apagar o arquivo do cartão, e por isso ela
    /// é um pedido **separado** do envio, e não o corpo da resposta do PUT: o
    /// que interessa não é o que o servidor diz ter recebido, é o que ele diz
    /// **ter guardado** depois de fechar o arquivo.
    ///
    /// Status 404 chega aqui como `StatusNaoOk`, e isso basta: para quem
    /// chama, "o servidor não tem" e "o servidor não respondeu direito" levam
    /// à mesma decisão — não apagar.
    virtual ResultadoHttp consulta_crc(const Url& url, const char* token,
                                       std::uint32_t* crc, Logger& log,
                                       std::uint32_t tempo_limite_ms) = 0;
};

}  // namespace coruja
