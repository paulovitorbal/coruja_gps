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
    /// Qualquer byte fora de `0x21`–`0x7E`. Ver `seguro_para_cabecalho`:
    /// um `\r\n` aqui vira cabeçalho a mais na requisição que o aparelho
    /// manda, e um espaço parte a linha de pedido em duas.
    CaractereInvalido,
};

const char* descreve(ErroUrl erro);

/// Decompõe `texto`. Devolve `ErroUrl::Nenhum` e preenche `destino` no sucesso.
///
/// Caminho ausente vira `/`. Porta ausente vira 80 ou 443 conforme o esquema.
///
/// **Não valida o host como nome**: saber se `exemplo.com` existe é trabalho
/// do DNS, e um host inexistente precisa falhar com a mensagem do DNS, não
/// com uma regra inventada aqui.
///
/// **Valida os bytes**, que é outra coisa: a URL inteira tem de ser
/// imprimível de ASCII (`seguro_para_cabecalho`). Ela vai crua para a linha
/// de pedido e para o `Host:`, e um `\r\n` vindo do `coruja.cfg` injetaria
/// cabeçalho na requisição do aparelho.
ErroUrl analisa_url(const char* texto, Url* destino);

/// Quantos caracteres do caminho podem ir para o log: tudo antes do `?`.
///
/// ⚠️ **O segredo do aparelho viaja na consulta.** Registrar `url.caminho`
/// inteiro o escreveria no `coruja.log` — que vai para o cartão, e o cartão
/// sai do carro. É a mesma regra da senha de Wi-Fi.
///
/// Mora aqui, e não dentro do cliente HTTP, porque os clientes só compilam
/// para o RP2350: enterrada lá, a máscara seria a única proteção de segredo
/// do projeto sem teste nenhum.
///
/// Usa-se com `%.*s`:
///
///     std::snprintf(msg, n, "GET %.*s",
///                   static_cast<int>(tamanho_sem_consulta(url.caminho)),
///                   url.caminho);
std::size_t tamanho_sem_consulta(const char* caminho);

}  // namespace coruja
