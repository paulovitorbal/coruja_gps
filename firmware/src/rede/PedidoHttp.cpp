#include "rede/PedidoHttp.h"

#include <cstdio>

#include "nucleo/Texto.h"

namespace coruja {

const char* token_para_cabecalho(const char* da_chamada,
                                 const char* guardado) {
    if (guardado == nullptr) { guardado = ""; }
    if (da_chamada == nullptr || da_chamada[0] == '\0') { return guardado; }
    return seguro_para_cabecalho(da_chamada) ? da_chamada : "";
}

std::size_t monta_pedido_http(char* destino, std::size_t capacidade,
                              const char* metodo, const Url& url,
                              const char* token, std::size_t corpo) {
    if (destino == nullptr || capacidade == 0 || metodo == nullptr) {
        return 0;
    }

    char tamanho[40] = {};
    if (corpo > 0) {
        std::snprintf(tamanho, sizeof tamanho, "Content-Length: %u\r\n",
                      static_cast<unsigned>(corpo));
    }

    const bool com_token = token != nullptr && token[0] != '\0';
    const int n = std::snprintf(
        destino, capacidade,
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: coruja-gps\r\n"
        "Accept-Encoding: identity\r\n"
        "%s%s%s"
        "%s"
        "Connection: close\r\n"
        "\r\n",
        metodo, url.caminho, url.host,
        com_token ? "X-Coruja-Token: " : "",
        com_token ? token : "",
        com_token ? "\r\n" : "",
        tamanho);

    // O `snprintf` devolve o que TERIA escrito: igual a capacidade ja quer
    // dizer que o terminador comeu o ultimo byte, e o pedido saiu truncado.
    if (n <= 0 || static_cast<std::size_t>(n) >= capacidade) { return 0; }
    return static_cast<std::size_t>(n);
}

}  // namespace coruja
