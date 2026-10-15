#include "nucleo/UbxDiagnostico.h"

#include <cstdio>

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
    destino->respondeu = true;
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
    destino->respondeu = true;
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
    destino->respondeu = true;
    return true;
}

std::size_t formata_diagnostico(const EstadoNavegacao& nav,
                                const EstadoRadio& radio, const EstadoAop& aop,
                                char* destino, std::size_t capacidade) {
    if (destino == nullptr || capacidade < kTamDiagnostico) {
        return 0;
    }

    // Três pedaços independentes, e cada um sabe dizer que não respondeu. A
    // alternativa — um `snprintf` só com tudo dentro — foi o que produziu a
    // linha mentirosa de 15/10/2026: ela não tinha onde pôr a ressalva.
    char rad[96];
    if (radio.respondeu) {
        std::snprintf(rad, sizeof rad,
                      "ruido %u | agc %u/8191 | antena %s | "
                      "interferencia %u/255 (%s)",
                      static_cast<unsigned>(radio.ruido),
                      static_cast<unsigned>(radio.agc), descreve(radio.antena),
                      static_cast<unsigned>(radio.interferencia),
                      descreve(radio.jamming));
    } else {
        std::snprintf(rad, sizeof rad, "MON-HW sem resposta");
    }

    char nav_txt[72];
    if (!nav.respondeu) {
        std::snprintf(nav_txt, sizeof nav_txt, "NAV-STATUS sem resposta");
    } else if (!nav.teve_fix) {
        // Respondeu DIZENDO que ainda não fixou. É informação, e diferente
        // de silêncio.
        std::snprintf(nav_txt, sizeof nav_txt,
                      "ttff do modulo ainda nao fixou (ligado ha %.1f s)",
                      nav.desde_boot_ms / 1000.0);
    } else {
        // O relógio do próprio módulo ao lado do TTFF: com ele, a diferença
        // para o nosso cronômetro vira aritmética em vez de inferência.
        std::snprintf(nav_txt, sizeof nav_txt,
                      "ttff do modulo %.1f s (ligado ha %.1f s)",
                      nav.ttff_ms / 1000.0, nav.desde_boot_ms / 1000.0);
    }

    char aop_txt[48];
    if (!aop.respondeu) {
        // ⚠️ "DESLIGADO" é uma AFIRMAÇÃO sobre a configuração do módulo.
        // Dizê-la porque a pergunta não voltou manda procurar defeito onde
        // não há.
        std::snprintf(aop_txt, sizeof aop_txt, "NAV-AOPSTATUS sem resposta");
    } else {
        std::snprintf(aop_txt, sizeof aop_txt, "aop %s%s",
                      aop.ligado ? "ligado" : "DESLIGADO",
                      aop.calculando ? ", calculando" : "");
    }

    const int n = std::snprintf(destino, capacidade, "radio: %s | %s | %s",
                                rad, nav_txt, aop_txt);
    if (n <= 0 || static_cast<std::size_t>(n) >= capacidade) {
        return 0;
    }
    return static_cast<std::size_t>(n);
}

}  // namespace coruja
