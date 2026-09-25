#include "nucleo/Ubx.h"

namespace coruja::ubx {
namespace {

void acumula(std::uint8_t b, std::uint8_t* ck_a, std::uint8_t* ck_b) {
    *ck_a = static_cast<std::uint8_t>(*ck_a + b);
    *ck_b = static_cast<std::uint8_t>(*ck_b + *ck_a);
}

void poe_u16(std::uint8_t* d, std::uint16_t v) {
    d[0] = static_cast<std::uint8_t>(v & 0xFFU);
    d[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFU);
}

void poe_u32(std::uint8_t* d, std::uint32_t v) {
    d[0] = static_cast<std::uint8_t>(v & 0xFFU);
    d[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFU);
    d[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFU);
    d[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFU);
}

/// Modo 8N1 da UART: charLen = 3 (8 bits) nos bits 6-7, parity = 100b
/// (nenhuma) nos bits 9-11, 1 stop bit nos bits 12-13. O bit 4 vem ligado no
/// valor de fábrica da u-blox, e é mantido por isso.
constexpr std::uint32_t kModo8N1 = 0x000008D0U;
/// Entrada e saída: UBX (bit 0) + NMEA (bit 1). A saída **precisa** dos dois
/// — NMEA para a RMC do RF01.1 e UBX para o ACK que confirma esta própria
/// configuração.
constexpr std::uint16_t kProtoUbxNmea = 0x0003U;

}  // namespace

const char* descreve(EventoUbx e) {
    switch (e) {
        case EventoUbx::Nada:              return "nada";
        case EventoUbx::Ack:               return "ack";
        case EventoUbx::Nak:               return "nak";
        case EventoUbx::Outro:             return "outro quadro";
        case EventoUbx::ChecksumInvalido:  return "checksum invalido";
        case EventoUbx::QuadroLongoDemais: return "quadro longo demais";
    }
    return "?";
}

void checksum(const std::uint8_t* dados, std::size_t tamanho,
              std::uint8_t* ck_a, std::uint8_t* ck_b) {
    *ck_a = 0;
    *ck_b = 0;
    for (std::size_t i = 0; i < tamanho; ++i) {
        acumula(dados[i], ck_a, ck_b);
    }
}

std::size_t monta(std::uint8_t classe, std::uint8_t id,
                  const std::uint8_t* payload, std::uint16_t tam_payload,
                  std::uint8_t* destino, std::size_t capacidade) {
    const std::size_t total = kSobrecarga + tam_payload;
    // Nunca escreve parcialmente: um quadro pela metade na UART e' pior que
    // quadro nenhum, porque o modulo tentaria interpreta-lo.
    if (destino == nullptr || capacidade < total) {
        return 0;
    }
    destino[0] = kSync1;
    destino[1] = kSync2;
    destino[2] = classe;
    destino[3] = id;
    poe_u16(&destino[4], tam_payload);
    for (std::uint16_t i = 0; i < tam_payload; ++i) {
        destino[6 + i] = payload[i];
    }
    std::uint8_t ck_a = 0;
    std::uint8_t ck_b = 0;
    // O checksum cobre de `classe` ate o fim do payload -- e nao o sync.
    checksum(&destino[2], 4U + tam_payload, &ck_a, &ck_b);
    destino[6 + tam_payload] = ck_a;
    destino[7 + tam_payload] = ck_b;
    return total;
}

std::size_t monta_cfg_rate(std::uint16_t periodo_ms, std::uint8_t* destino,
                           std::size_t capacidade) {
    std::uint8_t p[6];
    poe_u16(&p[0], periodo_ms);
    poe_u16(&p[2], 1);   // navRate: uma solucao por ciclo de medicao
    poe_u16(&p[4], 1);   // timeRef: tempo GPS
    return monta(kClasseCfg, kCfgRate, p, sizeof p, destino, capacidade);
}

std::size_t monta_cfg_msg(std::uint8_t classe_msg, std::uint8_t id_msg,
                          std::uint8_t taxa, std::uint8_t* destino,
                          std::size_t capacidade) {
    const std::uint8_t p[3] = {classe_msg, id_msg, taxa};
    return monta(kClasseCfg, kCfgMsg, p, sizeof p, destino, capacidade);
}

std::size_t monta_cfg_prt_uart(std::uint32_t baud, std::uint8_t* destino,
                               std::size_t capacidade) {
    std::uint8_t p[20] = {};
    p[0] = 1;                            // portID: UART1
    p[1] = 0;                            // reservado
    poe_u16(&p[2], 0);                   // txReady desligado
    poe_u32(&p[4], kModo8N1);
    poe_u32(&p[8], baud);
    poe_u16(&p[12], kProtoUbxNmea);      // inProtoMask
    poe_u16(&p[14], kProtoUbxNmea);      // outProtoMask
    poe_u16(&p[16], 0);                  // flags
    poe_u16(&p[18], 0);                  // reservado
    return monta(kClasseCfg, kCfgPrt, p, sizeof p, destino, capacidade);
}

std::size_t monta_cfg_cfg(std::uint32_t mascara_salvar,
                          std::uint8_t dispositivos, std::uint8_t* destino,
                          std::size_t capacidade) {
    std::uint8_t p[13] = {};
    poe_u32(&p[0], 0);                   // clearMask: nao apaga nada
    poe_u32(&p[4], mascara_salvar);
    poe_u32(&p[8], 0);                   // loadMask: nao recarrega nada
    p[12] = dispositivos;
    return monta(kClasseCfg, kCfgCfg, p, sizeof p, destino, capacidade);
}

// ------------------------------------------------------------------ leitura

void LeitorUbx::reinicia() {
    estado_ = Estado::Sync1;
    lidos_ = 0;
    invalidos_ = 0;
}

EventoUbx LeitorUbx::fecha_quadro() {
    if (classe_ == kClasseAck && tam_ == 2) {
        alvo_classe_ = payload_[0];
        alvo_id_ = payload_[1];
        if (id_ == kAckAck) { return EventoUbx::Ack; }
        if (id_ == kAckNak) { return EventoUbx::Nak; }
    }
    return EventoUbx::Outro;
}

EventoUbx LeitorUbx::consome(std::uint8_t byte) {
    switch (estado_) {
        case Estado::Sync1:
            if (byte == kSync1) { estado_ = Estado::Sync2; }
            return EventoUbx::Nada;

        case Estado::Sync2:
            if (byte == kSync2) {
                estado_ = Estado::Classe;
                ck_a_ = 0;
                ck_b_ = 0;
            } else if (byte == kSync1) {
                // Dois 0xB5 seguidos: o segundo ainda pode abrir um quadro.
                // Voltar cegamente ao inicio perderia o sincronismo aqui.
                estado_ = Estado::Sync2;
            } else {
                estado_ = Estado::Sync1;
            }
            return EventoUbx::Nada;

        case Estado::Classe:
            classe_ = byte;
            acumula(byte, &ck_a_, &ck_b_);
            estado_ = Estado::Id;
            return EventoUbx::Nada;

        case Estado::Id:
            id_ = byte;
            acumula(byte, &ck_a_, &ck_b_);
            estado_ = Estado::Tam1;
            return EventoUbx::Nada;

        case Estado::Tam1:
            tam_ = byte;
            acumula(byte, &ck_a_, &ck_b_);
            estado_ = Estado::Tam2;
            return EventoUbx::Nada;

        case Estado::Tam2: {
            tam_ = static_cast<std::uint16_t>(tam_ |
                   (static_cast<std::uint16_t>(byte) << 8));
            acumula(byte, &ck_a_, &ck_b_);
            if (tam_ > kMaxPayloadLido) {
                // Quase certamente nao e um quadro: um `0xB5 0x62` casual no
                // meio de dados NMEA. Volta a procurar sincronismo em vez de
                // engolir centenas de bytes bons.
                ++invalidos_;
                estado_ = Estado::Sync1;
                lidos_ = 0;
                return EventoUbx::QuadroLongoDemais;
            }
            lidos_ = 0;
            estado_ = (tam_ == 0) ? Estado::CkA : Estado::Payload;
            return EventoUbx::Nada;
        }

        case Estado::Payload:
            acumula(byte, &ck_a_, &ck_b_);
            if (lidos_ < kMaxPayloadGuardado) { payload_[lidos_] = byte; }
            ++lidos_;
            if (lidos_ >= tam_) { estado_ = Estado::CkA; }
            return EventoUbx::Nada;

        case Estado::CkA:
            ck_a_recebido_ = byte;
            estado_ = Estado::CkB;
            return EventoUbx::Nada;

        case Estado::CkB: {
            estado_ = Estado::Sync1;
            if (ck_a_recebido_ != ck_a_ || byte != ck_b_) {
                ++invalidos_;
                return EventoUbx::ChecksumInvalido;
            }
            return fecha_quadro();
        }
    }
    estado_ = Estado::Sync1;
    return EventoUbx::Nada;
}

}  // namespace coruja::ubx
