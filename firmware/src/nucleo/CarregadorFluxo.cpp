#include "nucleo/CarregadorFluxo.h"

#include <cstring>

namespace coruja {

CarregadorFluxo::CarregadorFluxo(Ponto* destino, std::size_t capacidade)
    : destino_(destino), capacidade_(capacidade) {}

void CarregadorFluxo::reinicia() {
    verificador_.reinicia();
    escritos_ = 0;
    parcial_n_ = 0;
    lat_anterior_ = -2147483647 - 1;
    erro_de_registro_ = ErroBase::Nenhum;
}

void CarregadorFluxo::consome_registro(const std::uint8_t* registro) {
    if (escritos_ >= capacidade_) {
        // Para de escrever, mas continua consumindo: o CRC precisa de todos os
        // bytes, e parar no meio daria "CRC invalido" para um arquivo que na
        // verdade só é grande demais para esta placa.
        if (erro_de_registro_ == ErroBase::Nenhum) {
            erro_de_registro_ = ErroBase::ExcedeuCapacidade;
        }
        return;
    }

    Ponto ponto;
    std::int32_t lat = 0;
    if (!decodifica_registro(registro, &ponto, &lat)) {
        if (erro_de_registro_ == ErroBase::Nenhum) {
            erro_de_registro_ = ErroBase::RegistroInvalido;
        }
        return;
    }
    if (lat < lat_anterior_) {
        if (erro_de_registro_ == ErroBase::Nenhum) {
            erro_de_registro_ = ErroBase::ForaDeOrdem;
        }
        return;
    }
    lat_anterior_ = lat;
    destino_[escritos_++] = ponto;
}

void CarregadorFluxo::alimenta(const std::uint8_t* bytes, std::size_t tamanho) {
    if (bytes == nullptr || tamanho == 0 || destino_ == nullptr) {
        return;
    }

    const std::size_t antes = verificador_.bytes_recebidos();
    verificador_.alimenta(bytes, tamanho);

    // Onde começa a parte deste pedaço que é dado, e não cabeçalho.
    std::size_t i = 0;
    if (antes < kTamCabecalho) {
        const std::size_t do_cabecalho = kTamCabecalho - antes;
        i = do_cabecalho < tamanho ? do_cabecalho : tamanho;
    }

    while (i < tamanho) {
        const std::size_t falta = kTamRegistro - parcial_n_;
        const std::size_t disponivel = tamanho - i;
        const std::size_t copiar = falta < disponivel ? falta : disponivel;
        std::memcpy(parcial_ + parcial_n_, bytes + i, copiar);
        parcial_n_ += copiar;
        i += copiar;
        if (parcial_n_ == kTamRegistro) {
            consome_registro(parcial_);
            parcial_n_ = 0;
        }
    }
}

ErroBase CarregadorFluxo::conclui() const {
    // O que o cabeçalho, a contagem e o CRC dizem vem primeiro — é a ordem do
    // `carrega_base()`, e mantê-la é o que faz os dois caminhos concordarem.
    const ErroBase pelo_formato = verificador_.conclui(capacidade_);
    if (pelo_formato != ErroBase::Nenhum) {
        return pelo_formato;
    }
    return erro_de_registro_;
}

}  // namespace coruja
