#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja::ubx {

/// Os dois bytes de sincronismo que abrem todo quadro UBX.
constexpr std::uint8_t kSync1 = 0xB5;
constexpr std::uint8_t kSync2 = 0x62;

/// Cabeçalho (2 sync + classe + id + 2 de tamanho) mais os 2 do checksum.
constexpr std::size_t kSobrecarga = 8;

/// Teto de payload aceito na leitura. Nada que interessa aqui passa disso, e
/// um tamanho absurdo é sinal de dessincronia — seguir em frente por 64 KiB
/// perderia todos os quadros bons do caminho.
constexpr std::uint16_t kMaxPayloadLido = 512;

/// Quanto do payload o leitor guarda. O que passa disso é somado ao checksum
/// e descartado, o que mantém a RAM constante sem deixar de validar o quadro
/// inteiro.
///
/// **Eram 8 bytes**, que bastavam para ACK e NAK. Passou a 64 por causa do
/// `MON-HW`: o indicador de interferência mora no byte **45**, depois de 17
/// bytes reservados, e é ele que diz se o receptor está cego por ruído. Os
/// 56 bytes a mais são o preço de poder responder essa pergunta.
/// ⚠️ **256, e o que manda e o `NAV-SAT`.** O `ACK` precisa de 2 bytes; o
/// `jamInd` do `MON-HW` mora no byte 45; o `NAV-SAT` traz 8 de cabecalho
/// mais 12 por satelite, e com o ceu cheio passa de 300.
///
/// Guardar tudo custaria RAM por um dado que so interessa resumido, entao
/// cabem 20 satelites e o resto e cortado -- o `numSvs` vem do CABECALHO e
/// continua exato, e quem resume marca que cortou. Ver `analisa_nav_sat`.
constexpr std::size_t kMaxPayloadGuardado = 8 + 12 * 20;

// ------------------------------------------------------------- classes/ids

constexpr std::uint8_t kClasseCfg = 0x06;
constexpr std::uint8_t kCfgPrt  = 0x00;
constexpr std::uint8_t kCfgMsg  = 0x01;
constexpr std::uint8_t kCfgRate = 0x08;
constexpr std::uint8_t kCfgCfg  = 0x09;

constexpr std::uint8_t kClasseAck = 0x05;
constexpr std::uint8_t kAckNak = 0x00;
constexpr std::uint8_t kAckAck = 0x01;

/// Classe das sentenças NMEA padrão, para o `CFG-MSG`.
constexpr std::uint8_t kClasseNmea = 0xF0;
constexpr std::uint8_t kNmeaGga = 0x00;
constexpr std::uint8_t kNmeaGll = 0x01;
constexpr std::uint8_t kNmeaGsa = 0x02;
constexpr std::uint8_t kNmeaGsv = 0x03;
constexpr std::uint8_t kNmeaRmc = 0x04;
constexpr std::uint8_t kNmeaVtg = 0x05;
constexpr std::uint8_t kNmeaTxt = 0x41;

constexpr std::uint8_t kCfgNav5  = 0x24;
constexpr std::uint8_t kCfgNavx5 = 0x23;

/// Classes de diagnóstico, só de leitura — ver `UbxDiagnostico.h` para o
/// motivo de cada uma existir.
constexpr std::uint8_t kClasseNav = 0x01;
constexpr std::uint8_t kNavStatus = 0x03;
constexpr std::uint8_t kNavSat = 0x35;
constexpr std::uint8_t kNavAopStatus = 0x60;
constexpr std::uint8_t kClasseMon = 0x0A;
constexpr std::uint8_t kMonHw = 0x09;

/// Bits de `deviceMask` do `CFG-CFG`.
constexpr std::uint8_t kDispBbr     = 0x01;
constexpr std::uint8_t kDispFlash   = 0x02;
constexpr std::uint8_t kDispEeprom  = 0x04;

// ------------------------------------------------------------------ escrita

/// Checksum Fletcher de 8 bits do UBX, sobre classe, id, tamanho e payload —
/// **não** sobre os bytes de sincronismo.
void checksum(const std::uint8_t* dados, std::size_t tamanho,
              std::uint8_t* ck_a, std::uint8_t* ck_b);

/// Monta um quadro completo em `destino`. Devolve o tamanho escrito, ou 0 se
/// não coube — nunca escreve parcialmente.
std::size_t monta(std::uint8_t classe, std::uint8_t id,
                  const std::uint8_t* payload, std::uint16_t tam_payload,
                  std::uint8_t* destino, std::size_t capacidade);

/// `CFG-RATE`: período de medição em ms. 250 ms são os 4 Hz do RF01.4.
std::size_t monta_cfg_rate(std::uint16_t periodo_ms, std::uint8_t* destino,
                           std::size_t capacidade);

/// `CFG-MSG` na forma curta de 3 bytes: taxa da mensagem na porta atual.
/// Taxa 0 desliga, 1 envia a cada época de navegação.
std::size_t monta_cfg_msg(std::uint8_t classe_msg, std::uint8_t id_msg,
                          std::uint8_t taxa, std::uint8_t* destino,
                          std::size_t capacidade);

/// `CFG-PRT` da UART1: baud rate, 8N1, entrada e saída em UBX+NMEA.
///
/// ⚠️ Os campos de bitmask vêm da **especificação do protocolo, não de
/// medição em bancada**. O juiz é o módulo: se ele responder `ACK`, aceitou;
/// se responder `NAK`, algum campo está errado. Por isso o RF01.2 só está
/// fechado quando o ACK for observado com o NEO-M8N em mãos — ver R-53.
///
/// Note ainda que a resposta a este comando chega no **baud novo**: quem
/// enviar tem de trocar a própria UART antes de esperar o ACK.
std::size_t monta_cfg_prt_uart(std::uint32_t baud, std::uint8_t* destino,
                               std::size_t capacidade);

/// Consulta: quadro de payload **zero**, que no UBX quer dizer "me mande esta
/// mensagem agora". A resposta vem com a mesma classe e id.
///
/// Consultar em vez de ligar a emissão periódica é deliberado: o diagnóstico
/// é olhado de cinco em cinco minutos, e uma mensagem de 60 bytes a 4 Hz
/// ocuparia banda permanente para responder a uma pergunta ocasional.
std::size_t monta_consulta(std::uint8_t classe, std::uint8_t id,
                           std::uint8_t* destino, std::size_t capacidade);

/// `CFG-NAVX5` ligando (ou desligando) a **previsão autônoma de órbita**.
///
/// O receptor usa as efemérides que já capturou para prever onde os
/// satélites estarão nos próximos dias, e parte com essa previsão em vez de
/// esperar a transmissão de órbita — que é o que faz um cold start levar
/// dezenas de segundos. Não precisa de rede: é cálculo no próprio módulo.
///
/// ⚠️ **Depende da BBR sobreviver ao desligamento**, porque é lá que a
/// previsão fica. Neste aparelho sabemos que sobrevive: foi exatamente isso
/// que causou o incidente de 08/10/2026, quando o módulo voltou de uma
/// parada curta ainda falando 115200 enquanto o Pico religava em 9600.
///
/// ⚠️ **Só o bit de AOP é aplicado.** O `mask1` seleciona quais campos valem,
/// e os outros 38 bytes vão zerados: sem essa seleção, este comando
/// reescreveria mínimo de satélites, limiar de C/N0 e rollover de semana com
/// zero — um efeito colateral que ninguém pediu.
/// `CFG-NAV5` com o modelo dinâmico **Automotivo**.
///
/// 🔴 **Nunca foi enviado até 2026-10-26**, e o receptor rodava no padrão de
/// fábrica, `Portable`. Os modelos diferem no quanto de aceleração o filtro
/// de navegação considera plausível; `Portable` supõe menos do que um carro
/// faz, e o filtro resiste a aceitar a mudança de velocidade — atraso que
/// aparece exatamente em arrancada e frenagem, que é onde se olha para o
/// velocímetro.
///
/// O `mask1` marca **só** o bit do modelo dinâmico. Sem isso os 36 bytes
/// restantes do payload, zerados, reescreveriam máscara de fixes, limiar de
/// elevação, limites de PDOP e tempo estático — ajustes que ninguém pediu.
std::size_t monta_cfg_nav5_automotivo(std::uint8_t* destino,
                                      std::size_t capacidade);

std::size_t monta_cfg_navx5_aop(bool liga, std::uint8_t* destino,
                                std::size_t capacidade);

/// `CFG-CFG`: persiste a configuração corrente nos dispositivos indicados.
std::size_t monta_cfg_cfg(std::uint32_t mascara_salvar,
                          std::uint8_t dispositivos, std::uint8_t* destino,
                          std::size_t capacidade);

// ------------------------------------------------------------------ leitura

enum class EventoUbx : std::uint8_t {
    Nada = 0,           ///< quadro ainda incompleto
    Ack,                ///< ACK-ACK: veja `classe_alvo()` / `id_alvo()`
    Nak,                ///< ACK-NAK: o módulo recusou o comando
    Outro,              ///< quadro válido que não é ACK nem NAK
    ChecksumInvalido,   ///< quadro completo, checksum errado
    QuadroLongoDemais,  ///< tamanho acima de `kMaxPayloadLido`: dessincronia
};

const char* descreve(EventoUbx evento);

/// Lê UBX byte a byte, **intercalado com NMEA na mesma UART**.
///
/// É streaming por necessidade, não por elegância: na linha do receptor os
/// quadros binários chegam entre sentenças de texto, e não há como delimitar
/// um buffer antes de saber onde o quadro começa. O `0xB5` também aparece
/// dentro de payloads, então perder o sincronismo é normal e a recuperação
/// faz parte do funcionamento, não do tratamento de erro.
///
/// **Limite conhecido:** um quadro truncado no meio do payload engole o
/// cabeçalho do quadro seguinte, cujo checksum então falha; o quadro
/// *depois* desse é lido normalmente. Sem relógio não há como expirar um
/// quadro pendente, e um byte-timeout seria complexidade especulativa para
/// um caso que só aparece com perda de bytes na UART. Custa um quadro de
/// atraso, e o RF01.2 reenvia.
class LeitorUbx {
public:
    /// Consome um byte. Devolve `Nada` até um quadro fechar.
    EventoUbx consome(std::uint8_t byte);

    /// Classe e id da mensagem que o ACK/NAK confirma ou recusa.
    std::uint8_t classe_alvo() const { return alvo_classe_; }
    std::uint8_t id_alvo() const { return alvo_id_; }

    /// Classe e id do último quadro completo lido, seja ele qual for.
    std::uint8_t classe() const { return classe_; }
    std::uint8_t id() const { return id_; }

    /// O payload do último quadro, até `kMaxPayloadGuardado` bytes.
    ///
    /// O tamanho devolvido é o que FOI GUARDADO, e não o que o quadro
    /// declarou: quem decodifica tem de recusar o payload curto em vez de
    /// ler além do que existe — e os decodificadores de `UbxDiagnostico.h`
    /// recusam.
    const std::uint8_t* payload() const { return payload_; }
    std::size_t tamanho_payload() const {
        return tam_ < kMaxPayloadGuardado ? tam_ : kMaxPayloadGuardado;
    }

    std::uint32_t quadros_invalidos() const { return invalidos_; }
    void reinicia();

private:
    enum class Estado : std::uint8_t {
        Sync1, Sync2, Classe, Id, Tam1, Tam2, Payload, CkA, CkB
    };

    EventoUbx fecha_quadro();

    Estado        estado_ = Estado::Sync1;
    std::uint8_t  classe_ = 0;
    std::uint8_t  id_ = 0;
    std::uint16_t tam_ = 0;
    std::uint16_t lidos_ = 0;
    std::uint8_t  payload_[kMaxPayloadGuardado] = {};
    std::uint8_t  ck_a_ = 0;
    std::uint8_t  ck_b_ = 0;
    std::uint8_t  ck_a_recebido_ = 0;
    std::uint8_t  alvo_classe_ = 0;
    std::uint8_t  alvo_id_ = 0;
    std::uint32_t invalidos_ = 0;
};

}  // namespace coruja::ubx
