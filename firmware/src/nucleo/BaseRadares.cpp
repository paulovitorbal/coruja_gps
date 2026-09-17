#include "nucleo/BaseRadares.h"

#include <cstdio>
#include <cstring>

#include "log/Logger.h"

namespace coruja {

namespace {

std::uint32_t le_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::int32_t le_i32(const std::uint8_t* p) {
    return static_cast<std::int32_t>(le_u32(p));
}

std::uint16_t le_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

constexpr const char* kOrigem = "base";

void registra(Logger* logger, Nivel nivel, const char* msg) {
    if (logger != nullptr) {
        logger->registra(nivel, kOrigem, msg);
    }
}

ResultadoCarga falha(Logger* logger, ErroBase erro) {
    registra(logger, Nivel::Error, descreve(erro));
    return {erro, 0};
}

}  // namespace

const char* descreve(ErroBase erro) {
    switch (erro) {
        case ErroBase::Nenhum:                return "sem erro";
        case ErroBase::TamanhoInvalido:       return "tamanho invalido";
        case ErroBase::MagicInvalido:         return "assinatura invalida";
        case ErroBase::VersaoInvalida:        return "versao nao suportada";
        case ErroBase::EscalaInvalida:        return "expoente de escala inesperado";
        case ErroBase::TamRegistroInvalido:   return "tamanho de registro inesperado";
        case ErroBase::BaseVazia:             return "base sem nenhum ponto";
        case ErroBase::ExcedeuTeto:           return "acima do teto de 40.000 pontos";
        case ErroBase::ContagemInconsistente: return "contagem discorda do tamanho";
        case ErroBase::ExcedeuCapacidade:     return "mais pontos do que a capacidade";
        case ErroBase::CrcInvalido:           return "CRC-32 nao confere";
        case ErroBase::ForaDeOrdem:           return "nao ordenado por latitude";
        case ErroBase::RegistroInvalido:      return "registro com campo fora do dominio";
    }
    return "erro desconhecido";
}

std::uint32_t crc32(const std::uint8_t* bytes, std::size_t tamanho) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < tamanho; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mascara = -(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mascara);
        }
    }
    return ~crc;
}

ResultadoCarga carrega_base(const std::uint8_t* bytes, std::size_t tamanho,
                            Ponto* destino, std::size_t capacidade,
                            Logger* logger) {
    if (bytes == nullptr || destino == nullptr || tamanho < kTamCabecalho) {
        return falha(logger, ErroBase::TamanhoInvalido);
    }
    if ((tamanho - kTamCabecalho) % kTamRegistro != 0) {
        return falha(logger, ErroBase::TamanhoInvalido);
    }
    if (le_u32(bytes) != kMagic) {
        return falha(logger, ErroBase::MagicInvalido);
    }
    if (le_u16(bytes + 4) != kVersao) {
        return falha(logger, ErroBase::VersaoInvalida);
    }
    if (bytes[6] != kExpoenteEscala) {
        return falha(logger, ErroBase::EscalaInvalida);
    }
    if (bytes[7] != kTamRegistro) {
        return falha(logger, ErroBase::TamRegistroInvalido);
    }

    const std::uint32_t declarados = le_u32(bytes + 8);
    if (declarados == 0) {
        return falha(logger, ErroBase::BaseVazia);
    }
    if (declarados > kTetoPontos) {
        return falha(logger, ErroBase::ExcedeuTeto);
    }
    const std::size_t reais = (tamanho - kTamCabecalho) / kTamRegistro;
    if (declarados != reais) {
        return falha(logger, ErroBase::ContagemInconsistente);
    }
    if (reais > capacidade) {
        return falha(logger, ErroBase::ExcedeuCapacidade);
    }

    const std::uint32_t crc_esperado = le_u32(bytes + 12);
    if (crc32(bytes + kTamCabecalho, tamanho - kTamCabecalho) != crc_esperado) {
        return falha(logger, ErroBase::CrcInvalido);
    }

    std::int32_t lat_anterior = -2147483647 - 1;
    for (std::size_t i = 0; i < reais; ++i) {
        const std::uint8_t* r = bytes + kTamCabecalho + i * kTamRegistro;

        const std::int32_t lat_e = le_i32(r);
        if (lat_e < lat_anterior) {
            return falha(logger, ErroBase::ForaDeOrdem);
        }
        lat_anterior = lat_e;

        const std::uint8_t flags   = r[10];
        const auto         sentido = static_cast<std::uint8_t>(flags & 0x03U);
        const auto         tipo    = static_cast<std::uint8_t>((flags >> 2) & 0x07U);
        if (!tipo_valido(tipo) || !sentido_valido(sentido)) {
            return falha(logger, ErroBase::RegistroInvalido);
        }

        destino[i] = Ponto{
            static_cast<float>(lat_e) / kEscala,
            static_cast<float>(le_i32(r + 4)) / kEscala,
            r[8],
            static_cast<std::uint16_t>(r[9] * 2),
            static_cast<TipoPonto>(tipo),
            static_cast<Sentido>(sentido),
        };
    }

    if (logger != nullptr) {
        char msg[64];
        std::snprintf(msg, sizeof msg, "%u pontos carregados",
                      static_cast<unsigned>(reais));
        logger->info(kOrigem, msg);
    }
    return {ErroBase::Nenhum, reais};
}

}  // namespace coruja
