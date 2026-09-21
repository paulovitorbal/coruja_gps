#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/Url.h"

namespace coruja {

class Logger;

enum class ErroHttp {
    Nenhum,
    UrlInvalida,
    TlsNaoSuportado,   ///< URL `https`: este cliente não fala TLS
    NaoIniciou,        ///< o pedido nem saiu (DNS, memória, socket)
    TempoEsgotado,
    StatusNaoOk,       ///< respondeu, mas não com 200
    Interrompida,      ///< conexão caiu no meio
};

const char* descreve(ErroHttp erro);

struct ResultadoHttp {
    ErroHttp      erro = ErroHttp::Nenhum;
    std::uint32_t status = 0;      ///< código HTTP, quando houve resposta
    std::size_t   recebidos = 0;   ///< bytes de corpo, sem cabeçalhos
    bool ok() const { return erro == ErroHttp::Nenhum; }
};

/// GET bloqueante sobre o cliente HTTP do lwIP.
///
/// O corpo é entregue **em pedaços, à medida que chega**, e nunca acumulado:
/// o `radares.bin` tem 214 KB e não há RAM sobrando com a base já reservada.
/// Quem chama decide o que fazer com cada pedaço — verificar, gravar no
/// cartão, ou ambos.
///
/// ⚠️ **Não fala TLS.** Uma URL `https` é recusada com `TlsNaoSuportado`, e
/// não baixada em claro: baixar em claro uma URL que diz `https` seria mentir
/// sobre o RF05.2 no exato ponto em que ele importa.
class ClienteHttp {
public:
    /// Chamada a cada pedaço recebido. Não pode bloquear.
    using AoReceber = void (*)(void* contexto, const std::uint8_t* bytes,
                               std::size_t tamanho);

    ResultadoHttp baixa(const Url& url, AoReceber ao_receber, void* contexto,
                        Logger& log, std::uint32_t tempo_limite_ms = 30'000);
};

}  // namespace coruja
