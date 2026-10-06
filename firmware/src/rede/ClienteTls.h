#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/Url.h"
#include "rede/Baixador.h"
#include "rede/Enviador.h"

namespace coruja {

class Logger;

/// Cliente HTTP **único** do aparelho, sobre `altcp` — com ou sem TLS.
///
/// Substitui o `ClienteHttp` (que só baixava, pelo `http_client` do lwIP) e o
/// `ClienteEnvio` (que só enviava, por TCP cru). Os dois existiam porque o
/// `http_client` não faz PUT nem aceita cabeçalho próprio; com TLS o
/// transporte teve de ser reescrito de qualquer forma, e aí manter duas
/// implementações do mesmo diálogo deixou de se justificar.
///
/// **É a mesma classe que fala `http://` e `https://`.** A camada `altcp` do
/// lwIP é uma indireção sobre o TCP: o código do cliente é idêntico, e o que
/// muda é o alocador de conexão. `http` continua existindo para a bancada e
/// para rede local; em campo o RF05.2 pede TLS.
///
/// ⚠️ **Verifica o certificado de verdade.** O padrão do lwIP é
/// `MBEDTLS_SSL_VERIFY_OPTIONAL`, que verifica e **não aborta** — ver o
/// `lwipopts.h`. Aqui o modo é `REQUIRED`, e as autoridades aceitas estão em
/// `RaizesConfiaveis.h`.
///
/// ⚠️ **Manda SNI.** Sem o nome do servidor no ClientHello, o Cloudflare não
/// tem como escolher qual certificado servir — um endereço só atende milhões
/// de domínios. Sem isso o handshake falha com uma mensagem que não explica
/// nada.
///
/// ⚠️ **Precisa da hora certa.** O prazo do certificado é julgado contra o
/// relógio; sem ele acertado, tudo é recusado por "ainda não vale". Ver
/// `SincronizadorHora` e `PlataformaMbedtls`.
class ClienteTls : public Baixador, public Enviador {
public:
    ~ClienteTls() override;

    /// GET com o corpo entregue **em pedaços, à medida que chega**: o
    /// `radares.bin` tem 214 KB e não há RAM para ele inteiro.
    ResultadoHttp baixa(const Url& url, AoReceber ao_receber, void* contexto,
                        Logger& log,
                        std::uint32_t tempo_limite_ms) override;

    ResultadoHttp envia(const Url& url, const char* token, FonteDeBytes fonte,
                        void* contexto, std::size_t tamanho, Logger& log,
                        std::uint32_t tempo_limite_ms) override;

    ResultadoHttp consulta_crc(const Url& url, const char* token,
                               std::uint32_t* crc, Logger& log,
                               std::uint32_t tempo_limite_ms) override;

    /// O segredo que acompanha **todas** as requisições, inclusive os GET da
    /// base. Fica no cliente e não em cada chamada porque é identidade do
    /// aparelho, não parâmetro de pedido.
    ///
    /// ⚠️ Vai no cabeçalho `X-Coruja-Token` e **nunca** no log nem na URL.
    void define_token(const char* token);

    /// Libera a configuração de TLS, com as raízes já interpretadas.
    ///
    /// Ela é criada na primeira conexão e reaproveitada: interpretar três
    /// certificados em PEM custa alguns KB e milissegundos, e refazer isso a
    /// cada pedido não traria nada. Mas ela vive na arena emprestada da base
    /// — então tem de morrer **antes** de a memória voltar.
    void libera_configuracao();

private:
    void* configuracao_ = nullptr;   ///< `struct altcp_tls_config*`
    char  token_[64] = {};
};

}  // namespace coruja
