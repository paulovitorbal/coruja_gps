#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Decodificadores das mensagens UBX que explicam **por que** o fix demora.
///
/// Existem por causa de 14/10/2026: o primeiro fix levou **2 min 48 s** com o
/// carro parado um minuto numa garagem descoberta — seis vezes o cold start
/// de 26 s do datasheet. Sobraram duas hipóteses opostas, e nenhuma delas se
/// decide por inspeção:
///
/// 1. o receptor parte **sem dado nenhum** — efeméride e almanaque perdidos,
///    e cada ligação recomeça do zero;
/// 2. o receptor parte **cego** — o front-end de GNSS está afogado em ruído,
///    e o nosso próprio aparelho é o suspeito principal: o SPI do painel a
///    16 MHz e o do cartão correm a centímetros da antena.
///
/// A primeira se conserta com configuração; a segunda, com ferro de solda e
/// posição de antena. Estas três mensagens separam uma da outra, e quem
/// responde é o módulo.

/// Estado da antena, do campo `aStatus` do `MON-HW`.
enum class AntenaGps : std::uint8_t {
    Iniciando = 0,
    Desconhecida,
    Ok,
    Curto,
    Aberta,
};

/// Monitor de interferência, dos bits 2-3 de `flags` do `MON-HW`.
///
/// ⚠️ `Desconhecido` **não** é "sem interferência": é o monitor desligado.
/// Confundir os dois leva a concluir que a linha está limpa quando ninguém
/// olhou.
enum class Jamming : std::uint8_t {
    Desconhecido = 0,  ///< monitor desligado
    Limpo,             ///< mediu e não há interferência significativa
    Aviso,             ///< interferência visível, mas o fix se mantém
    Critico,           ///< interferência visível e SEM fix
};

const char* descreve(AntenaGps a);
const char* descreve(Jamming j);

/// `UBX-NAV-STATUS` (0x01 0x03): o que o receptor diz do próprio fix.
struct EstadoNavegacao {
    std::uint8_t  tipo_fix = 0;        ///< 0 nenhum, 2 2D, 3 3D, 5 só hora
    bool          fix_valido = false;  ///< `gpsFixOK`: o módulo assina a solução
    bool          teve_fix = false;    ///< houve TTFF nesta ligação
    /// Tempo até o primeiro fix **medido pelo módulo**, em ms.
    ///
    /// ⚠️ Zero significa "ainda não houve fix nesta ligação", e não "fixou
    /// instantaneamente". Daí `teve_fix` existir à parte.
    std::uint32_t ttff_ms = 0;
    std::uint32_t desde_boot_ms = 0;   ///< `msss`, ligação do RECEPTOR
};

/// `UBX-MON-HW` (0x0A 0x09): o estado do front-end de rádio.
struct EstadoRadio {
    std::uint16_t ruido = 0;           ///< `noisePerMS`
    std::uint16_t agc = 0;             ///< `agcCnt`, de 0 a 8191
    AntenaGps     antena = AntenaGps::Iniciando;
    std::uint8_t  interferencia = 0;   ///< `jamInd`, de 0 a 255
    Jamming       jamming = Jamming::Desconhecido;
};

/// `UBX-NAV-AOPSTATUS` (0x01 0x60): a previsão de órbita está mesmo ligada?
struct EstadoAop {
    bool ligado = false;      ///< `aopCfg` bit 0
    /// Calculando agora. **Ocioso é o estado normal** de quem está ligado e
    /// em dia — ler isso como "não funciona" faria desligar o que funciona.
    bool calculando = false;
};

/// Os três devolvem `false` quando o payload é curto demais para o campo que
/// interessa. Payload curto é dessincronia ou outra versão de protocolo, e
/// decodificar lixo seria pior que não decodificar.
bool analisa_nav_status(const std::uint8_t* payload, std::size_t tamanho,
                        EstadoNavegacao* destino);
bool analisa_mon_hw(const std::uint8_t* payload, std::size_t tamanho,
                    EstadoRadio* destino);
bool analisa_nav_aopstatus(const std::uint8_t* payload, std::size_t tamanho,
                           EstadoAop* destino);

}  // namespace coruja
