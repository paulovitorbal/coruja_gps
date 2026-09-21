#include "nucleo/Url.h"

#include <cstring>

namespace coruja {
namespace {

bool copia(char* destino, std::size_t capacidade, const char* inicio,
           std::size_t tamanho) {
    if (tamanho >= capacidade) {
        return false;
    }
    std::memcpy(destino, inicio, tamanho);
    destino[tamanho] = '\0';
    return true;
}

}  // namespace

const char* descreve(ErroUrl erro) {
    switch (erro) {
        case ErroUrl::Nenhum:              return "ok";
        case ErroUrl::Vazia:               return "URL vazia";
        case ErroUrl::EsquemaDesconhecido: return "esquema nao e http:// nem https://";
        case ErroUrl::SemHost:             return "URL sem host";
        case ErroUrl::PortaInvalida:       return "porta invalida";
        case ErroUrl::LongaDemais:         return "URL longa demais";
        case ErroUrl::Ipv6NaoSuportado:    return "IPv6 literal nao suportado";
    }
    return "erro desconhecido";
}

ErroUrl analisa_url(const char* texto, Url* destino) {
    if (texto == nullptr || destino == nullptr || texto[0] == '\0') {
        return ErroUrl::Vazia;
    }
    *destino = Url{};

    const char* resto = nullptr;
    if (std::strncmp(texto, "http://", 7) == 0) {
        resto = texto + 7;
        destino->tls = false;
        destino->porta = 80;
    } else if (std::strncmp(texto, "https://", 8) == 0) {
        resto = texto + 8;
        destino->tls = true;
        destino->porta = 443;
    } else {
        return ErroUrl::EsquemaDesconhecido;
    }

    // O caminho começa na primeira `/` DEPOIS do host. Procurar a primeira `/`
    // do texto inteiro pegaria as duas de `http://`.
    const char* barra = std::strchr(resto, '/');
    const char* fim_autoridade = barra != nullptr ? barra : resto + std::strlen(resto);

    for (const char* p = resto; p < fim_autoridade; ++p) {
        if (*p == '[' || *p == ']') {
            return ErroUrl::Ipv6NaoSuportado;
        }
    }

    // `:` dentro da autoridade separa a porta, e a busca vai do fim para o
    // início. O IPv6 literal já foi recusado acima justamente porque aqui o
    // último `:` dele passaria por separador de porta.
    const char* doispontos = nullptr;
    for (const char* p = fim_autoridade; p > resto; --p) {
        if (*(p - 1) == ':') {
            doispontos = p - 1;
            break;
        }
    }

    const char* fim_host = doispontos != nullptr ? doispontos : fim_autoridade;
    const std::size_t tam_host = static_cast<std::size_t>(fim_host - resto);
    if (tam_host == 0) {
        return ErroUrl::SemHost;
    }
    if (!copia(destino->host, sizeof destino->host, resto, tam_host)) {
        return ErroUrl::LongaDemais;
    }

    if (doispontos != nullptr) {
        std::uint32_t porta = 0;
        const char*   p = doispontos + 1;
        if (p == fim_autoridade) {
            return ErroUrl::PortaInvalida;
        }
        for (; p < fim_autoridade; ++p) {
            if (*p < '0' || *p > '9') {
                return ErroUrl::PortaInvalida;
            }
            porta = porta * 10U + static_cast<std::uint32_t>(*p - '0');
            if (porta > 65535U) {
                return ErroUrl::PortaInvalida;
            }
        }
        if (porta == 0) {
            return ErroUrl::PortaInvalida;
        }
        destino->porta = static_cast<std::uint16_t>(porta);
    }

    if (barra == nullptr) {
        destino->caminho[0] = '/';
        destino->caminho[1] = '\0';
        return ErroUrl::Nenhum;
    }
    if (!copia(destino->caminho, sizeof destino->caminho, barra,
               std::strlen(barra))) {
        return ErroUrl::LongaDemais;
    }
    return ErroUrl::Nenhum;
}

}  // namespace coruja
