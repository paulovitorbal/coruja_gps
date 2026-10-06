#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/Url.h"

namespace coruja {

class Logger;

enum class ErroHttp {
    Nenhum,
    UrlInvalida,
    NaoIniciou,        ///< o pedido nem saiu (DNS, memória, socket)
    TempoEsgotado,
    StatusNaoOk,       ///< respondeu, mas não com 200
    Interrompida,      ///< conexão caiu no meio
};

inline const char* descreve(ErroHttp erro) {
    switch (erro) {
        case ErroHttp::Nenhum:          return "ok";
        case ErroHttp::UrlInvalida:     return "URL invalida";
        case ErroHttp::NaoIniciou:      return "o pedido nao saiu (DNS? memoria?)";
        case ErroHttp::TempoEsgotado:   return "tempo esgotado";
        case ErroHttp::StatusNaoOk:     return "servidor respondeu com status != 200";
        case ErroHttp::Interrompida:    return "conexao interrompida no meio";
    }
    return "erro desconhecido";
}

struct ResultadoHttp {
    ErroHttp      erro = ErroHttp::Nenhum;
    std::uint32_t status = 0;      ///< código HTTP, quando houve resposta
    std::size_t   recebidos = 0;   ///< bytes de corpo, sem cabeçalhos
    bool ok() const { return erro == ErroHttp::Nenhum; }
};

/// Quem busca bytes de uma URL.
///
/// Houve um `TlsNaoSuportado` neste enum, de quando o cliente recusava
/// `https` em vez de baixar em claro. Ele saiu com a adoção do `ClienteTls`:
/// valor de erro que não pode mais acontecer confunde quem lê o `switch` e
/// faz procurar um caminho que não existe.
///
/// A entrega é por retorno de chamada e não por buffer: a base tem 214 KB e
/// não há onde guardá-la inteira ao lado dos 281 KB que a própria base
/// reserva em RAM. Quem recebe decide o que fazer com cada pedaço.
class Baixador {
public:
    using AoReceber = void (*)(void* contexto, const std::uint8_t* bytes,
                               std::size_t tamanho);

    virtual ~Baixador() = default;

    virtual ResultadoHttp baixa(const Url& url, AoReceber ao_receber,
                                void* contexto, Logger& log,
                                std::uint32_t tempo_limite_ms) = 0;
};

}  // namespace coruja
