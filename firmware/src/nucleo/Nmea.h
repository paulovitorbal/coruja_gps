#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Uma leitura do GPS, já em unidades de uso.
///
/// Só o que o RF01 pede, mais data e hora — o relógio da tela vem daqui,
/// porque **não há RTC com bateria no BOM** (§4.1).
struct Telemetria {
    float lat = 0.0F;             ///< graus decimais, negativo ao sul
    float lon = 0.0F;             ///< graus decimais, negativo a oeste
    float velocidade_kmh = 0.0F;
    float rumo_graus = 0.0F;      ///< 0,0 a 359,9

    /// ⚠️ O rumo vem **vazio** na RMC com o veículo parado, e isso é normal —
    /// sem deslocamento não há direção a informar. Quem usar o rumo tem de
    /// olhar esta bandeira, senão trata zero como "apontando para o norte".
    bool rumo_valido = false;

    /// Hora **UTC**. A conversão para o fuso local é de quem exibe: o fuso é
    /// constante de compilação (ADR 0002), não configuração.
    std::uint16_t ano = 0;
    std::uint8_t  mes = 0, dia = 0;
    std::uint8_t  hora = 0, minuto = 0, segundo = 0;
    bool data_valida = false;
};

enum class ErroNmea {
    Nenhum,
    Vazia,
    SemCifrao,          ///< não começa com `$`
    SemAsterisco,       ///< sem o `*` que delimita o checksum
    ChecksumMalformado, ///< `*` presente, mas sem dois dígitos hexadecimais
    ChecksumInvalido,
    NaoEhRmc,
    CamposDeMenos,
    SemFix,             ///< sentença íntegra, campo de status = `V`
    CampoInvalido,
};

const char* descreve(ErroNmea erro);

/// Valida o checksum NMEA: XOR de tudo entre o `$` e o `*`.
bool checksum_valido(const char* sentenca, std::size_t tamanho);

/// Interpreta uma sentença RMC.
///
/// **Aceita qualquer talker de duas letras**, não só `GN` e `GP`. O RF01.1
/// exige os dois porque o talker muda com o modo GNSS, e um parser fixado em
/// `GN` não casaria nada em modo GPS-only — *"falha silenciosa total"*. Aceitar
/// qualquer talker é a mesma proteção levada ao limite: se o módulo um dia
/// emitir `GL` ou `GA`, a sentença continua sendo uma RMC válida, e recusá-la
/// custaria um fix. O checksum é que impede aceitar lixo.
///
/// Não escreve em `destino` quando devolve erro — inclusive em `SemFix`, para
/// que uma sentença sem fix não zere a última posição conhecida por acidente.
ErroNmea analisa_rmc(const char* sentenca, std::size_t tamanho,
                     Telemetria* destino);

}  // namespace coruja
