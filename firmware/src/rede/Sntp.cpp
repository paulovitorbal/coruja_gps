#include "rede/Sntp.h"

#include <cstring>

#include "nucleo/TempoUtc.h"

namespace coruja {
namespace {

// RFC 4330 §4.
constexpr std::size_t kDeslocamentoOrigem = 24;      // `originate`
constexpr std::size_t kDeslocamentoTransmissao = 40;  // `transmit`

constexpr std::uint8_t kModoCliente = 3;
constexpr std::uint8_t kModoServidor = 4;
constexpr std::uint8_t kVersao = 4;
/// A menor versao que este cliente le. A 3 e a 4 nao diferem em nada do
/// que ele usa, e ha servidor publico respondendo na 3: recusa-la seria
/// perder hora por nada.
constexpr std::uint8_t kVersaoMinima = 3;
constexpr std::uint8_t kSaltoNaoSincronizado = 3;
constexpr std::uint8_t kEstratoMaximo = 15;

std::uint32_t le_u32_be(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

std::uint64_t le_u64_be(const std::uint8_t* p) {
    return (static_cast<std::uint64_t>(le_u32_be(p)) << 32) |
           le_u32_be(p + 4);
}

void escreve_u64_be(std::uint8_t* p, std::uint64_t v) {
    for (std::size_t i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>(v >> (56 - 8 * i));
    }
}

}  // namespace

const char* descreve(ErroNtp erro) {
    switch (erro) {
        case ErroNtp::Nenhum:          return "ok";
        case ErroNtp::TamanhoInvalido: return "resposta curta demais";
        case ErroNtp::NaoEhResposta:   return "nao e resposta de servidor";
        case ErroNtp::NaoSincronizado: return "o servidor diz estar sem sincronia";
        case ErroNtp::EstratoInvalido: return "estrato invalido (kiss-o-death?)";
        case ErroNtp::TimestampZero:   return "sem hora de transmissao";
        case ErroNtp::ForaDeFaixa:     return "hora implausivel";
        case ErroNtp::NaoEhDoNossoPedido:
            return "resposta nao e do pedido que saiu daqui";
        case ErroNtp::VersaoInvalida:  return "versao de NTP que nao falamos";
    }
    return "erro desconhecido";
}

void monta_pedido_ntp(std::uint8_t* pacote, std::uint64_t nonce) {
    if (pacote == nullptr) { return; }
    std::memset(pacote, 0, kTamanhoPacoteNtp);
    // LI = 0 (sem aviso), VN = 4, Mode = 3 (cliente).
    pacote[0] = static_cast<std::uint8_t>((kVersao << 3) | kModoCliente);
    // A hora de transmissao do pedido, que o servidor devolve no
    // `originate`. Aqui ela nao e hora: e o sorteio que amarra a resposta
    // ao pedido. O aparelho nem sabe a hora -- e por isso que esta
    // perguntando.
    escreve_u64_be(pacote + kDeslocamentoTransmissao, nonce);
}

ErroNtp le_resposta_ntp(const std::uint8_t* pacote, std::size_t tamanho,
                        std::uint64_t nonce, std::int64_t* segundos_unix) {
    if (pacote == nullptr || segundos_unix == nullptr ||
            tamanho < kTamanhoPacoteNtp) {
        return ErroNtp::TamanhoInvalido;
    }

    const std::uint8_t salto = (pacote[0] >> 6) & 0x03U;
    const std::uint8_t versao = (pacote[0] >> 3) & 0x07U;
    const std::uint8_t modo = pacote[0] & 0x07U;
    const std::uint8_t estrato = pacote[1];

    if (modo != kModoServidor) { return ErroNtp::NaoEhResposta; }
    if (versao < kVersaoMinima || versao > kVersao) {
        return ErroNtp::VersaoInvalida;
    }
    // ANTES de olhar a hora: um pacote que nao devolve o nonce nao foi
    // mandado por quem viu o pedido, e nada mais nele interessa. Ver
    // `monta_pedido_ntp`.
    if (le_u64_be(pacote + kDeslocamentoOrigem) != nonce) {
        return ErroNtp::NaoEhDoNossoPedido;
    }
    // Indicador de salto 3 e o servidor dizendo "meu relogio nao esta
    // sincronizado". Ele responde assim quando acabou de subir e ainda nao
    // se acertou com o proprio par -- e a hora que ele manda nesse estado
    // nao vale nada.
    if (salto == kSaltoNaoSincronizado) { return ErroNtp::NaoSincronizado; }
    // Estrato 0 e `kiss-o'-death`: o servidor esta pedindo para o cliente
    // parar de incomodar, e o pacote carrega um codigo de texto no lugar do
    // identificador de referencia -- nao uma hora.
    if (estrato == 0 || estrato > kEstratoMaximo) {
        return ErroNtp::EstratoInvalido;
    }

    const std::uint32_t bruto = le_u32_be(pacote + kDeslocamentoTransmissao);
    if (bruto == 0) { return ErroNtp::TimestampZero; }

    // A era do NTP vira em 2036, quando o contador de 32 bits estoura.
    // Depois disso o bit mais alto zera, e um cliente que ignorasse isso
    // voltaria a 1900 de uma vez. A regra da RFC 4330 §3: com o bit alto
    // ligado a hora e da era 0 (1968--2036); desligado, da era 1.
    constexpr std::int64_t kEra = 4'294'967'296LL;  // 2^32
    const bool era_zero = (bruto & 0x80000000U) != 0;
    const std::int64_t segundos_ntp =
        static_cast<std::int64_t>(bruto) + (era_zero ? 0 : kEra);

    const std::int64_t unix = segundos_ntp - kNtpParaUnix;
    if (!de_segundos(unix).plausivel()) { return ErroNtp::ForaDeFaixa; }

    *segundos_unix = unix;
    return ErroNtp::Nenhum;
}

}  // namespace coruja
