#pragma once
#include <cstddef>
#include <cstdint>

#include "rede/Baixador.h"

namespace coruja {

class Logger;

// `ErroHttp`, `ResultadoHttp` e `descreve()` vivem em `Baixador.h`.

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
class ClienteHttp : public Baixador {
public:
    /// Chamada a cada pedaço recebido. Não pode bloquear.
    using AoReceber = void (*)(void* contexto, const std::uint8_t* bytes,
                               std::size_t tamanho);

    ResultadoHttp baixa(const Url& url, AoReceber ao_receber, void* contexto,
                        Logger& log, std::uint32_t tempo_limite_ms = 30'000) override;
};

}  // namespace coruja
