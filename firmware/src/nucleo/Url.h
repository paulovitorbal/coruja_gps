#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/Configuracao.h"

namespace coruja {

/// URL decomposta no que a pilha de rede precisa: host, porta e caminho.
///
/// Sem `std::string` e sem heap, como todo o resto do caminho crítico. Os
/// tamanhos saem do `kMaxUrl` da configuração, então uma URL que o
/// `gera_config.py` aceitou cabe aqui por construção.
struct Url {
    char          host[kMaxUrl + 1] = {};
    char          caminho[kMaxUrl + 1] = {};
    std::uint16_t porta = 0;
    /// `true` para `https://`. **O cliente HTTP deste firmware não fala TLS**
    /// — quem receber uma URL `https` deve recusar explicitamente, em vez de
    /// baixar em claro e fingir que cumpriu o RF05.2.
    bool tls = false;
};

enum class ErroUrl {
    Nenhum,
    Vazia,
    EsquemaDesconhecido,  ///< não começa com `http://` nem `https://`
    SemHost,
    PortaInvalida,
    LongaDemais,
    /// `http://[::1]:8080/x`. Este firmware não fala IPv6, e aceitar o
    /// literal entre colchetes só empurraria a falha para o DNS, com uma
    /// mensagem que não explica nada.
    Ipv6NaoSuportado,
};

const char* descreve(ErroUrl erro);

/// Decompõe `texto`. Devolve `ErroUrl::Nenhum` e preenche `destino` no sucesso.
///
/// Caminho ausente vira `/`. Porta ausente vira 80 ou 443 conforme o esquema.
/// **Não valida o host**: isso é trabalho do DNS, e um host inválido precisa
/// falhar com a mensagem do DNS, não com uma regra inventada aqui.
ErroUrl analisa_url(const char* texto, Url* destino);

}  // namespace coruja
