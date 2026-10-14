#include "nucleo/UbxDiagnostico.h"

namespace coruja {

namespace {

/// Os deslocamentos vêm do manual de protocolo do M8 (UBX-13003221). Estão
/// nomeados porque número solto no meio de um `payload[45]` não se confere
/// contra documento nenhum.
namespace navstatus {
constexpr std::size_t kTamanho = 16;
constexpr std::size_t kTipoFix = 4;
constexpr std::size_t kFlags = 5;
constexpr std::size_t kTtff = 8;
constexpr std::size_t kMsss = 12;
constexpr std::uint8_t kBitFixOk = 0x01;
}  // namespace navstatus

namespace monhw {
constexpr std::size_t kTamanho = 60;
constexpr std::size_t kRuido = 16;
constexpr std::size_t kAgc = 18;
constexpr std::size_t kAntena = 20;
constexpr std::size_t kFlags = 22;
/// ⚠️ **45**, depois dos 17 bytes de `VP`. É este campo que obrigou o leitor
/// de UBX a guardar mais que os 8 bytes de que o ACK precisa.
constexpr std::size_t kJamInd = 45;
}  // namespace monhw

namespace aopstatus {
constexpr std::size_t kTamanho = 16;
constexpr std::size_t kConfig = 4;
constexpr std::size_t kStatus = 5;
constexpr std::uint8_t kBitUsaAop = 0x01;
}  // namespace aopstatus

std::uint16_t le_u16(const std::uint8_t* p) {
    // Little-endian, como todo campo do UBX.
    return static_cast<std::uint16_t>(p[0] |
                                      (static_cast<std::uint16_t>(p[1]) << 8));
}

std::uint32_t le_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

const char* descreve(AntenaGps a) {
    switch (a) {
        case AntenaGps::Iniciando:    return "iniciando";
        case AntenaGps::Desconhecida: return "desconhecida";
        case AntenaGps::Ok:           return "ok";
        case AntenaGps::Curto:        return "CURTO";
        case AntenaGps::Aberta:       return "ABERTA";
    }
    return "?";
}

const char* descreve(Jamming j) {
    switch (j) {
        case Jamming::Desconhecido: return "nao medido";
        case Jamming::Limpo:        return "limpo";
        case Jamming::Aviso:        return "AVISO";
        case Jamming::Critico:      return "CRITICO";
    }
    return "?";
}

bool analisa_nav_status(const std::uint8_t* payload, std::size_t tamanho,
                        EstadoNavegacao* destino) {
    if (payload == nullptr || destino == nullptr ||
        tamanho < navstatus::kTamanho) {
        return false;
    }
    destino->tipo_fix = payload[navstatus::kTipoFix];
    destino->fix_valido =
        (payload[navstatus::kFlags] & navstatus::kBitFixOk) != 0;
    destino->ttff_ms = le_u32(&payload[navstatus::kTtff]);
    destino->desde_boot_ms = le_u32(&payload[navstatus::kMsss]);
    // Zero é "ainda não houve fix", e a bandeira própria evita que alguém
    // leia isso como "fixou instantaneamente".
    destino->teve_fix = destino->ttff_ms != 0;
    return true;
}

bool analisa_mon_hw(const std::uint8_t* payload, std::size_t tamanho,
                    EstadoRadio* destino) {
    if (payload == nullptr || destino == nullptr ||
        tamanho < monhw::kTamanho) {
        return false;
    }
    destino->ruido = le_u16(&payload[monhw::kRuido]);
    destino->agc = le_u16(&payload[monhw::kAgc]);
    const std::uint8_t antena = payload[monhw::kAntena];
    destino->antena = antena <= static_cast<std::uint8_t>(AntenaGps::Aberta)
                          ? static_cast<AntenaGps>(antena)
                          : AntenaGps::Desconhecida;
    destino->interferencia = payload[monhw::kJamInd];
    // Bits 2-3 de `flags`.
    destino->jamming =
        static_cast<Jamming>((payload[monhw::kFlags] >> 2) & 0x03);
    return true;
}

bool analisa_nav_aopstatus(const std::uint8_t* payload, std::size_t tamanho,
                           EstadoAop* destino) {
    if (payload == nullptr || destino == nullptr ||
        tamanho < aopstatus::kTamanho) {
        return false;
    }
    destino->ligado =
        (payload[aopstatus::kConfig] & aopstatus::kBitUsaAop) != 0;
    destino->calculando = payload[aopstatus::kStatus] != 0;
    return true;
}

}  // namespace coruja
