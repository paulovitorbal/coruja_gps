#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/Ponto.h"

namespace coruja {

class Logger;

/// Por que a base falhou a carga. O firmware precisa distinguir estes casos
/// porque o RF07 exige mensagens diferentes: cartao ausente o motorista
/// resolve inserindo o cartao, base corrompida ele nao resolve dirigindo.
enum class ErroBase {
    Nenhum,
    TamanhoInvalido,   ///< menor que o cabecalho, ou nao multiplo do registro
    MagicInvalido,
    VersaoInvalida,
    EscalaInvalida,
    TamRegistroInvalido,
    BaseVazia,              ///< n_pontos == 0; a especificacao recusa
    ExcedeuTeto,            ///< acima de kTetoPontos: estouraria a RAM
    ContagemInconsistente,  ///< cabecalho e tamanho do arquivo discordam
    ExcedeuCapacidade,      ///< mais pontos do que o destino aguenta
    CrcInvalido,
    ForaDeOrdem,            ///< nao esta ordenado por latitude
    RegistroInvalido,       ///< TYPE ou DirType fora do dominio
};

const char* descreve(ErroBase erro);

/// Resultado da carga. `pontos` sempre e zero em caso de erro: a base e
/// carregada por inteiro ou nao e carregada — nao existe base parcial, porque
/// um alerta que falta e pior que nenhum alerta.
struct ResultadoCarga {
    ErroBase    erro   = ErroBase::Nenhum;
    std::size_t pontos = 0;
    bool ok() const { return erro == ErroBase::Nenhum; }
};

/// Layout do `radares.bin`, espelhando formato_dados.md §2.
/// Cabecalho de 16 B: magic[4] | versao u16 | exp_escala u8 | tam_registro u8 |
/// n_pontos u32 | crc32 u32. Equivale ao `<4sHBBII` do `formato_radares.py`,
/// que e o contrato compartilhado entre conversor e firmware.
constexpr std::uint32_t kMagic          = 0x31524452U;  // "RDR1" little-endian
constexpr std::uint16_t kVersao         = 1;
constexpr std::uint8_t  kExpoenteEscala = 5;
constexpr float         kEscala         = 100000.0F;
constexpr std::size_t   kTamCabecalho   = 16;
constexpr std::size_t   kTamRegistro    = 12;

/// Teto de pontos que a carga integral aceita. Nao e limite de formato, e
/// protecao de memoria: acima disso os 12 B por registro estouram a SRAM e o
/// firmware deve recusar o arquivo em vez de travar (formato_dados.md §2).
constexpr std::uint32_t kTetoPontos = 40000;

/// Decodifica um arquivo `radares.bin` ja em memoria para um vetor de `Ponto`.
///
/// Nao le do cartao: recebe os bytes crus. Essa separacao e o que permite
/// testar a validacao inteira no host, contra o arquivo real de 18.294 pontos,
/// sem SD e sem Pico.
///
/// Valida, na ordem: tamanho, magic, versao, escala, contagem, capacidade,
/// CRC-32, ordenacao por latitude e o dominio de cada registro. Mesma ordem e
/// mesmos critérios do `formato_radares.py`, que e o contrato compartilhado.
ResultadoCarga carrega_base(const std::uint8_t* bytes, std::size_t tamanho,
                            Ponto* destino, std::size_t capacidade,
                            Logger* logger = nullptr);

/// CRC-32 (polinomio IEEE 802.3 refletido, 0xEDB88320), identico ao
/// `zlib.crc32` usado pelo conversor.
std::uint32_t crc32(const std::uint8_t* bytes, std::size_t tamanho);

}  // namespace coruja
