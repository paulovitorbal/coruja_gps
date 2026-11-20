#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Tamanho de um pacote SNTP sem autenticação (RFC 4330 §4).
constexpr std::size_t kTamanhoPacoteNtp = 48;

/// Porta padrão do NTP.
constexpr std::uint16_t kPortaNtp = 123;

/// Segundos entre 1900-01-01 e 1970-01-01.
///
/// O NTP conta de 1900 e o mundo conta de 1970. Setenta anos com dezessete
/// bissextos: 70·365 + 17 = 25.567 dias.
constexpr std::int64_t kNtpParaUnix = 2'208'988'800LL;

enum class ErroNtp : std::uint8_t {
    Nenhum,
    TamanhoInvalido,     ///< resposta curta demais para ser um pacote NTP
    NaoEhResposta,       ///< campo `mode` não é 4 (servidor)
    NaoSincronizado,     ///< indicador de salto 3: o servidor se declara perdido
    EstratoInvalido,     ///< 0 é "kiss-o'-death"; acima de 15 é inválido
    TimestampZero,       ///< o servidor não preencheu a hora de transmissão
    ForaDeFaixa,         ///< a hora não é plausível para este aparelho
    /// O campo `originate` não traz o nonce que foi no pedido: quem
    /// respondeu não viu o pedido, ou respondeu a outro.
    NaoEhDoNossoPedido,
    VersaoInvalida,      ///< campo `VN` fora de 3–4
};

const char* descreve(ErroNtp erro);

/// Monta o pedido: 48 bytes, cliente, versão 4, com `nonce` no campo
/// `transmit` (RFC 4330 §4).
///
/// **O nonce é a única amarra entre pedido e resposta** que o SNTP simples
/// oferece: o servidor devolve esses oito bytes no campo `originate`, e quem
/// não viu o pedido não tem como preenchê-los. Quem o escolhe tem de sortear
/// — no aparelho, o TRNG do RP2350 pelo `get_rand_64()`, a mesma fonte da
/// entropia do TLS.
///
/// O resto é zero de propósito. Um cliente SNTP simples não precisa preencher
/// mais nada, e campos inventados só dariam ao servidor motivo para recusar.
void monta_pedido_ntp(std::uint8_t* pacote, std::uint64_t nonce);

/// Lê a hora de transmissão da resposta, em segundos desde 1970.
///
/// **Valida antes de acreditar.** Um servidor pode responder dizendo que ele
/// próprio está perdido (indicador de salto 3), ou mandar um pacote de
/// "kiss-o'-death" com estrato 0 — e os dois chegam como respostas normais.
/// Aceitar qualquer coisa que tenha 48 bytes poria o relógio do aparelho,
/// e com ele a validação de certificado, na mão do primeiro pacote que
/// chegasse.
///
/// `nonce` é o que foi no `transmit` do pedido, e a resposta tem de
/// devolvê-lo no `originate`. Sem isso, qualquer pacote que chegasse à porta
/// durante a espera virava a hora do aparelho — e com ela a validação de
/// certificado — sem precisar nem ter visto o pedido.
///
/// ⚠️ **Isto ainda NÃO autentica nada.** NTP simples é UDP sem assinatura, e
/// o nonce não protege de quem está no caminho: quem vê o pedido copia o
/// número. O que ele tira do jogo é quem só sabe o endereço e a porta. Para
/// valer mais seria preciso NTS, que não cabe nesta placa — é o custo
/// conhecido de dar prioridade ao NTP, e está assumido (ver
/// `SincronizadorHora`).
///
/// A parte fracionária é descartada: o aparelho não tem o que fazer com
/// milissegundos, e carregá-los só daria precisão de mentira.
ErroNtp le_resposta_ntp(const std::uint8_t* pacote, std::size_t tamanho,
                        std::uint64_t nonce, std::int64_t* segundos_unix);

}  // namespace coruja
