#include "nucleo/Crc32.h"

namespace coruja {

void Crc32::alimenta(const std::uint8_t* bytes, std::size_t tamanho) {
    if (bytes == nullptr) {
        return;
    }
    std::uint32_t crc = acumulador_;
    for (std::size_t i = 0; i < tamanho; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            // Sem tabela de 1 KiB: a 214 KB por atualização, o custo do laço
            // por bit é irrelevante perto do tempo de rede, e a tabela ocuparia
            // memória que a base quer.
            const std::uint32_t mascara = -(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mascara);
        }
    }
    acumulador_ = crc;
}

std::uint32_t crc32(const std::uint8_t* bytes, std::size_t tamanho) {
    Crc32 acumulador;
    acumulador.alimenta(bytes, tamanho);
    return acumulador.valor();
}

}  // namespace coruja
