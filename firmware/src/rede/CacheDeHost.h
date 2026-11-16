#pragma once
#include <cstdint>

#include "nucleo/Url.h"

namespace coruja {

/// Por quanto tempo um nome já resolvido continua valendo.
///
/// Generoso de propósito: quem garante a correção não é o prazo, é o
/// `esquece()` — uma falha de conexão descarta o endereço na hora, e a
/// tentativa seguinte resolve de novo. O prazo só existe para que um aparelho
/// ligado o dia inteiro não carregue para sempre o endereço de uma sessão
/// antiga.
///
/// Cinco minutos cobrem uma remessa inteira (ver `kTetoDaRemessaMs`), que é o
/// caso que motivou isto.
constexpr std::uint32_t kValidadeDoHostMs = 300'000;

/// Lembra que um nome já foi resolvido nesta sessão — e só isso.
///
/// ## Por que existe
///
/// Cada pedido HTTP chamava `resolve()`. Uma remessa de doze arquivos são um
/// PUT e uma consulta de CRC por arquivo: **vinte e quatro resoluções do
/// mesmo nome**, cada uma com sua chance de não responder a tempo, e cada
/// falha condenando o arquivo da vez. O lwIP tem cache próprio, mas ele é
/// governado pelo TTL que o servidor de nomes devolve — e um roteiro
/// doméstico costuma devolver TTL curto, ou zero, para os nomes que ele mesmo
/// distribui. Contar com isso é contar com a configuração da casa.
///
/// ## O que NÃO mora aqui
///
/// O endereço. Ele é um `ip_addr_t` do lwIP, e esta classe precisa compilar
/// no host para ser testada — o `ClienteTls.cpp`, não. É a mesma divisão da
/// `CaixaDns`, e pelo mesmo motivo: enterrada lá, esta seria mais uma guarda
/// sem teste nenhum.
class CacheDeHost {
public:
    explicit CacheDeHost(std::uint32_t validade_ms = kValidadeDoHostMs)
        : validade_ms_(validade_ms) {}

    /// Este host foi resolvido e a resposta ainda vale?
    ///
    /// **Só então** quem chama pode usar o endereço que guardou ao lado.
    bool serve(const char* host, std::uint32_t agora_ms) const;

    /// Guarda que `host` acabou de ser resolvido.
    ///
    /// Um host que não cabe no buffer não é guardado — guardá-lo truncado
    /// faria dois nomes com o mesmo começo se confundirem, e o pedido iria
    /// para o endereço do outro.
    void guarda(const char* host, std::uint32_t agora_ms);

    /// Descarta o que estiver guardado.
    ///
    /// Chamado quando um pedido não chega a resposta nenhuma: pode ser
    /// justamente o endereço que está errado. Esquecer custa uma resolução;
    /// insistir num endereço morto custa a remessa inteira.
    void esquece();

private:
    /// Subtração sem sinal, como no `PrazoDeProgresso`: correta na virada dos
    /// 32 bits, que chega a cada ~49 dias de aparelho ligado.
    std::uint32_t idade(std::uint32_t agora_ms) const {
        return agora_ms - guardado_em_ms_;
    }

    char          host_[kMaxUrl + 1] = {};
    std::uint32_t guardado_em_ms_ = 0;
    std::uint32_t validade_ms_;
    bool          tem_ = false;
};

}  // namespace coruja
