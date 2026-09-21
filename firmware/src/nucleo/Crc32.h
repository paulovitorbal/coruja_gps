#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// CRC-32 incremental (polinômio IEEE 802.3 refletido, `0xEDB88320`),
/// idêntico ao `zlib.crc32` do conversor e do servidor.
///
/// Existe na forma incremental porque o download OTA **não cabe em RAM junto
/// com a base**: o `radares.bin` tem 214 KB e o vetor de pontos já reserva
/// 281 KB dos 520 KB da placa. Guardar o arquivo inteiro para só então
/// verificá-lo não é uma opção, então o CRC é alimentado pedaço a pedaço, na
/// ordem em que os pacotes chegam, e o arquivo nunca existe inteiro em lugar
/// nenhum.
///
/// A função livre `crc32()` continua existindo para quem já tem os bytes na
/// mão, e é implementada sobre esta classe — um só laço, uma só tabela de
/// verdade sobre o polinômio.
class Crc32 {
public:
    void alimenta(const std::uint8_t* bytes, std::size_t tamanho);

    /// O CRC dos bytes alimentados até aqui. Pode ser chamado a qualquer
    /// momento: não fecha nem invalida o acumulador.
    std::uint32_t valor() const { return ~acumulador_; }

    void reinicia() { acumulador_ = kInicial; }

private:
    static constexpr std::uint32_t kInicial = 0xFFFFFFFFU;
    std::uint32_t acumulador_ = kInicial;
};

std::uint32_t crc32(const std::uint8_t* bytes, std::size_t tamanho);

}  // namespace coruja
