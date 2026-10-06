#include "rede/Sntp.h"

#include <cstring>

#include "nucleo/TempoUtc.h"

namespace coruja {
namespace {

constexpr std::size_t kDeslocamentoTransmissao = 40;  // RFC 4330 §4

constexpr std::uint8_t kModoCliente = 3;
constexpr std::uint8_t kModoServidor = 4;
constexpr std::uint8_t kVersao = 4;
constexpr std::uint8_t kSaltoNaoSincronizado = 3;
constexpr std::uint8_t kEstratoMaximo = 15;

std::uint32_t le_u32_be(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
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
    }
    return "erro desconhecido";
}

void monta_pedido_ntp(std::uint8_t* pacote) {
    if (pacote == nullptr) { return; }
    std::memset(pacote, 0, kTamanhoPacoteNtp);
    // LI = 0 (sem aviso), VN = 4, Mode = 3 (cliente).
    pacote[0] = static_cast<std::uint8_t>((kVersao << 3) | kModoCliente);
}

ErroNtp le_resposta_ntp(const std::uint8_t* pacote, std::size_t tamanho,
                        std::int64_t* segundos_unix) {
    if (pacote == nullptr || segundos_unix == nullptr ||
            tamanho < kTamanhoPacoteNtp) {
        return ErroNtp::TamanhoInvalido;
    }

    const std::uint8_t salto = (pacote[0] >> 6) & 0x03U;
    const std::uint8_t modo = pacote[0] & 0x07U;
    const std::uint8_t estrato = pacote[1];

    if (modo != kModoServidor) { return ErroNtp::NaoEhResposta; }
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
