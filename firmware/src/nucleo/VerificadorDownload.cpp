#include "nucleo/VerificadorDownload.h"

#include <cstring>

namespace coruja {
namespace {

std::uint16_t le_u16(const std::uint8_t* b) {
    return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
}

std::uint32_t le_u32(const std::uint8_t* b) {
    return static_cast<std::uint32_t>(b[0]) |
           (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) |
           (static_cast<std::uint32_t>(b[3]) << 24);
}

}  // namespace

CabecalhoBase le_cabecalho(const std::uint8_t* bytes) {
    CabecalhoBase c;
    c.magic        = le_u32(bytes);
    c.versao       = le_u16(bytes + 4);
    c.exp_escala   = bytes[6];
    c.tam_registro = bytes[7];
    c.n_pontos     = le_u32(bytes + 8);
    c.crc          = le_u32(bytes + 12);
    return c;
}

void VerificadorDownload::reinicia() {
    crc_.reinicia();
    cabecalho_ = CabecalhoBase{};
    recebidos_ = 0;
}

void VerificadorDownload::alimenta(const std::uint8_t* bytes,
                                   std::size_t tamanho) {
    if (bytes == nullptr || tamanho == 0) {
        return;
    }

    // Parte que ainda pertence ao cabeçalho. Vale zero na esmagadora maioria
    // das chamadas, mas não em todas: nada garante que o primeiro pacote traga
    // 16 bytes, e um cabeçalho partido ao meio é a classe de defeito que só
    // aparece na rede de outra pessoa.
    if (recebidos_ < kTamCabecalho) {
        const std::size_t falta = kTamCabecalho - recebidos_;
        const std::size_t copiar = tamanho < falta ? tamanho : falta;
        std::memcpy(buffer_cabecalho_ + recebidos_, bytes, copiar);
        recebidos_ += copiar;
        bytes += copiar;
        tamanho -= copiar;
        if (recebidos_ == kTamCabecalho) {
            cabecalho_ = le_cabecalho(buffer_cabecalho_);
        }
    }

    if (tamanho > 0) {
        crc_.alimenta(bytes, tamanho);
        recebidos_ += tamanho;
    }
}

ErroBase VerificadorDownload::conclui(std::size_t capacidade) const {
    if (recebidos_ < kTamCabecalho) {
        return ErroBase::TamanhoInvalido;
    }
    if (bytes_de_dados() % kTamRegistro != 0) {
        return ErroBase::TamanhoInvalido;
    }
    if (cabecalho_.magic != kMagic) {
        return ErroBase::MagicInvalido;
    }
    if (cabecalho_.versao != kVersao) {
        return ErroBase::VersaoInvalida;
    }
    if (cabecalho_.exp_escala != kExpoenteEscala) {
        return ErroBase::EscalaInvalida;
    }
    if (cabecalho_.tam_registro != kTamRegistro) {
        return ErroBase::TamRegistroInvalido;
    }
    if (cabecalho_.n_pontos == 0) {
        return ErroBase::BaseVazia;
    }
    if (cabecalho_.n_pontos > kTetoPontos) {
        return ErroBase::ExcedeuTeto;
    }
    if (cabecalho_.n_pontos != pontos_recebidos()) {
        // Também é aqui que cai o download TRUNCADO — conexão caída no meio,
        // que é a falha mais provável de todas num carro em movimento. O
        // cabeçalho promete N pontos e chegaram menos.
        return ErroBase::ContagemInconsistente;
    }
    if (pontos_recebidos() > capacidade) {
        return ErroBase::ExcedeuCapacidade;
    }
    if (crc_calculado() != cabecalho_.crc) {
        return ErroBase::CrcInvalido;
    }
    return ErroBase::Nenhum;
}

}  // namespace coruja
