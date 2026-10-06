#pragma once
#include <cstddef>
#include <cstdint>

#include "rede/Enviador.h"
#include "rede/RespostaHttp.h"

namespace coruja {

class Logger;

/// Quanto da resposta o cliente guarda. Só interessam a linha de status e, na
/// consulta, oito caracteres de CRC em hexadecimal. 256 cobre os cabeçalhos
/// que o servidor de referência manda com folga, e o excedente é descartado
/// sem drama — não há nada depois disso que este cliente leia.
constexpr std::size_t kMaxRespostaEnvio = 256;

/// PUT e GET bloqueantes sobre TCP cru do lwIP.
///
/// **Não usa o `http_client` do lwIP**, que só sabe fazer GET. Daí o TCP na
/// mão: montar a requisição, empurrar o corpo conforme a janela abre, e ler a
/// resposta até os cabeçalhos.
///
/// ⚠️ **Não fala TLS**, como o `ClienteHttp`. Uma URL `https` é recusada com
/// `TlsNaoSuportado` em vez de ser mandada em claro — e aqui isso importa
/// mais, porque o que viaja é o segredo do aparelho junto com o conteúdo.
///
/// ⚠️ **Nunca escreve o token no log.** Ele vai no cabeçalho e não aparece em
/// nenhuma das mensagens; a linha de log do pedido mostra método, host e
/// caminho, e para aí.
class ClienteEnvio : public Enviador {
public:
    ResultadoHttp envia(const Url& url, const char* token, FonteDeBytes fonte,
                        void* contexto, std::size_t tamanho, Logger& log,
                        std::uint32_t tempo_limite_ms) override;

    ResultadoHttp consulta_crc(const Url& url, const char* token,
                               std::uint32_t* crc, Logger& log,
                               std::uint32_t tempo_limite_ms) override;
};

}  // namespace coruja
