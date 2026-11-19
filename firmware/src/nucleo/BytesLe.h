#pragma once
#include <cstdint>

namespace coruja {

/// Inteiros little-endian lidos de um buffer de bytes.
///
/// **Existiam três cópias disto** — na base de radares, no verificador de
/// download e no diagnóstico UBX —, byte a byte iguais. Os três leem formatos
/// little-endian (o `radares.bin`, que nós mesmos geramos, e o UBX do
/// u-blox), e uma correção numa cópia não alcançaria as outras duas.
///
/// Monta-se byte a byte, e não por `memcpy` de um `uint32_t`: isto também
/// compila para o ARM, onde o buffer pode vir desalinhado, e aí um acesso de
/// 32 bits a endereço ímpar é comportamento indefinido.
///
/// ⚠️ **Não serve para o NTP**, que é big-endian — ver `Sntp.cpp`. Ter a
/// ordem no nome é o que impede trocar um pelo outro por engano.
inline std::uint16_t le_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      static_cast<std::uint16_t>(p[1] << 8));
}

inline std::uint32_t le_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

/// Com sinal, para os campos de coordenada do `radares.bin`. A conversão de
/// `uint32_t` para `int32_t` é definida desde o C++20 e, na prática, é
/// complemento de dois em todo compilador que este projeto usa.
inline std::int32_t le_i32(const std::uint8_t* p) {
    return static_cast<std::int32_t>(le_u32(p));
}

}  // namespace coruja
