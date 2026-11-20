#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/CaoDeGuarda.h"
#include "nucleo/Url.h"
#include "rede/Baixador.h"
#include "rede/CacheDeHost.h"
#include "rede/Enviador.h"

namespace coruja {

/// Quanto tempo sem UM byte andar antes de desistir de um pedido HTTP.
///
/// Vale para todos os pedidos; o teto é que varia com o que se espera de
/// cada um. Quinze segundos é bem mais do que qualquer troca sadia leva, e
/// bem menos do que o teto, então é ele que responde quando a rede some.
constexpr std::uint32_t kInatividadeHttpMs = 15'000;

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
    ///
    /// Devolve `false` e fica **sem token** quando ele traz algo que não seja
    /// imprimível de ASCII. Um `\r\n` aqui não seria um token esquisito: seria
    /// um cabeçalho a mais na requisição que o aparelho manda, montado por
    /// quem escreveu no cartão. Ficar sem token faz o servidor recusar com
    /// 401, que é visível — guardar o token torto não seria.
    bool define_token(const char* token);

    /// Quem alimentar enquanto este cliente espera a rede. Nulo (o padrao) e
    /// "ninguem vigia".
    ///
    /// O cliente alimenta a cada volta dos seus laços de espera, DEPOIS de a
    /// chamada de poll voltar, e antes de registrar uma desistencia no log.
    /// O porque, e por que isso nao e "alimentar por tempo", esta em
    /// `CaoDeGuarda.h` e em `espera_vigiada`. O ponteiro nao e dono.
    void define_cao(CaoDeGuarda* cao) { cao_ = cao; }

    /// O maior uso do heap do lwIP já alcançado, e o total, em bytes.
    ///
    /// É de lá que sai a sessão TLS — `altcp_tls_create_config_client` aloca
    /// por `mem_calloc`, não pelo alocador do mbedTLS. Sem este número, o
    /// `MEM_SIZE` continua sendo escolhido no olho, e a única forma de
    /// descobrir que ficou curto é um `sem memoria para a sessao TLS` em
    /// campo.
    ///
    /// `false` quando o lwIP foi compilado sem estatísticas.
    static bool pico_do_heap(std::size_t* usado, std::size_t* total);

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
    CaoDeGuarda* cao_ = nullptr;

    /// Qual nome já foi resolvido, e por quanto tempo isso ainda vale.
    ///
    /// Vive no cliente, e não no orquestrador, porque quem resolve é o
    /// cliente — e assim o OTA e a remessa aproveitam o mesmo endereço sem
    /// combinarem nada.
    CacheDeHost cache_host_;

    /// O endereço que acompanha o `cache_host_`, guardado **opaco**.
    ///
    /// Seria um `ip_addr_t`, mas esse é tipo do lwIP e este cabeçalho é
    /// incluído por quem não o tem. O `.cpp` confere o tamanho num
    /// `static_assert` — se o lwIP mudar, a compilação para em vez de
    /// escrever fora.
    std::uint8_t endereco_guardado_[16] = {};
};

}  // namespace coruja
