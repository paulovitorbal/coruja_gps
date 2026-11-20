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
    /// O aperto de mão TLS recusou o certificado do servidor.
    ///
    /// Era `Interrompida` até 16/11/2026, e a diferença importa a quem chama:
    /// a conexão que cai pode passar na tentativa seguinte, o certificado
    /// recusado não. O motivo mais comum é o relógio — sem hora acertada,
    /// todo certificado "ainda não vale" —, e ele não anda durante um envio.
    CertificadoRecusado,
};

inline const char* descreve(ErroHttp erro) {
    switch (erro) {
        case ErroHttp::Nenhum:          return "ok";
        case ErroHttp::UrlInvalida:     return "URL invalida";
        case ErroHttp::NaoIniciou:      return "o pedido nao saiu (DNS? memoria?)";
        case ErroHttp::TempoEsgotado:   return "tempo esgotado";
        case ErroHttp::StatusNaoOk:     return "servidor respondeu com status != 200";
        case ErroHttp::Interrompida:    return "conexao interrompida no meio";
        case ErroHttp::CertificadoRecusado:
            return "certificado recusado (relogio? cadeia?)";
    }
    return "erro desconhecido";
}

struct ResultadoHttp {
    ErroHttp      erro = ErroHttp::Nenhum;
    std::uint32_t status = 0;      ///< código HTTP, quando houve resposta
    std::size_t   recebidos = 0;   ///< bytes de corpo, sem cabeçalhos
    bool ok() const { return erro == ErroHttp::Nenhum; }
};

/// O que um pedido que **falhou** diz de si — e da tentativa seguinte.
///
/// Mora junto do `ResultadoHttp` porque são dois a lê-lo, a remessa e o OTA,
/// e a leitura tem sutileza que não pode divergir entre eles (vale o status,
/// e não o erro). Era privada da `RemessaDados` até 17/11/2026, e o OTA,
/// sem ela, repetia toda falha três vezes — um 401 inclusive — e a tela
/// dizia DOWNLOAD INCOMPLETO.
enum class FalhaHttp : std::uint8_t {
    Url,             ///< o cliente nem montou o pedido: é configuração
    SemResposta,     ///< nada voltou: DNS, conexão, tempo, queda
    ErroNoServidor,  ///< voltou erro de momento: 5xx, 408, 429
    GrandeDemais,    ///< 413: o servidor não aceita este tamanho
    Recusado,        ///< voltou recusa: 3xx e 4xx
    Certificado,     ///< o TLS recusou o certificado do servidor
};

/// Só faz sentido para `!r.ok()`.
inline FalhaHttp classifica_falha(const ResultadoHttp& r) {
    if (r.erro == ErroHttp::CertificadoRecusado) { return FalhaHttp::Certificado; }
    // O cliente nem montou o pedido: host vazio, ou cabecalho que nao coube.
    // E configuracao, como a URL que nao forma.
    if (r.erro == ErroHttp::UrlInvalida) { return FalhaHttp::Url; }
    // Vale o STATUS, e nao o erro. O servidor recusa sem ler o corpo e fecha
    // (`_descarta_corpo`), e o RST pode chegar logo atras da resposta: o
    // cliente devolve `Interrompida`, mas com o 401 que chegou a ler.
    //
    // Abaixo de 300 nao houve resposta que servisse: nenhuma linha de status,
    // ou uma que caiu antes de terminar, ou que veio sem o CRC.
    if (r.status < 300) { return FalhaHttp::SemResposta; }
    if (r.status == 413) { return FalhaHttp::GrandeDemais; }
    // Os dois 4xx que pedem, eles mesmos, outra tentativa.
    if (r.status == 408 || r.status == 429) { return FalhaHttp::ErroNoServidor; }
    // 3xx junto com 4xx: o aparelho nao segue redirecionamento, e o 301 de um
    // `http://` que o proxy manda para `https://` volta a cada tentativa.
    if (r.status < 500) { return FalhaHttp::Recusado; }
    return FalhaHttp::ErroNoServidor;
}

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
    /// Devolve `false` para **parar** o download: o resto do corpo não é
    /// pedido, a conexão é abortada e `baixa` volta com `Interrompida`.
    /// Quem pediu para parar sabe por quê; o cliente não precisa saber.
    using AoReceber = bool (*)(void* contexto, const std::uint8_t* bytes,
                               std::size_t tamanho);

    virtual ~Baixador() = default;

    virtual ResultadoHttp baixa(const Url& url, AoReceber ao_receber,
                                void* contexto, Logger& log,
                                std::uint32_t tempo_limite_ms) = 0;
};

}  // namespace coruja
