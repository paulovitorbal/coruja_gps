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
    if (c.versao != kVersaoSemData) {
        c.ano = le_u16(bytes + 16);
        c.mes = bytes[18];
        c.dia = bytes[19];
    }
    return c;
}

void VerificadorDownload::reinicia() {
    crc_.reinicia();
    cabecalho_ = CabecalhoBase{};
    // **Esquecer isto quebrava a segunda tentativa**: o cabecalho da
    // tentativa anterior seguiria valendo, e os primeiros bytes do download
    // novo entrariam no CRC como se fossem dados.
    cabecalho_lido_ = false;
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
    // **O cabecalho tem tamanho variavel**, e isso muda a conta: sao 16
    // bytes na versao 1 e 20 na 2, e a versao so se conhece depois de ler os
    // 6 primeiros. Le-se ate 16, decide-se, e so entao se sabe se os 4
    // seguintes sao cabecalho ou ja sao dados. Errar aqui nao da erro
    // visivel -- corrompe o CRC, e o arquivo e recusado por um motivo que
    // nao e o verdadeiro.
    while (!cabecalho_lido_ && tamanho > 0) {
        const std::size_t alvo =
            recebidos_ < kTamCabecalhoSemData
                ? kTamCabecalhoSemData
                : le_cabecalho(buffer_cabecalho_).tamanho();
        if (recebidos_ >= alvo) {
            cabecalho_ = le_cabecalho(buffer_cabecalho_);
            cabecalho_lido_ = true;
            break;
        }
        const std::size_t falta = alvo - recebidos_;
        const std::size_t copiar = tamanho < falta ? tamanho : falta;
        std::memcpy(buffer_cabecalho_ + recebidos_, bytes, copiar);
        recebidos_ += copiar;
        bytes += copiar;
        tamanho -= copiar;
    }

    if (tamanho > 0) {
        crc_.alimenta(bytes, tamanho);
        recebidos_ += tamanho;
    }
}

ErroBase VerificadorDownload::conclui(std::size_t capacidade) const {
    if (!cabecalho_lido_) {
        return ErroBase::TamanhoInvalido;
    }
    // **Magic e versao ANTES do tamanho**, e a ordem virou obrigatoria com o
    // cabecalho de tamanho variavel: nao da para conferir se o arquivo tem
    // um numero redondo de registros sem antes saber onde o cabecalho
    // acaba, e isso depende da versao, que so vale se o magic conferir.
    //
    // A ordem e a mesma do `carrega_base`, e ha teste que exige que os dois
    // deem o MESMO veredito -- dois caminhos de leitura que discordam sobre
    // por que um arquivo e ruim seriam dois diagnosticos para o mesmo
    // defeito.
    if (cabecalho_.magic != kMagic) {
        return ErroBase::MagicInvalido;
    }
    // Aceita as duas versoes na LEITURA. Um cartao com base v1 continua
    // valido e o aparelho so fica sem a data; recusa-lo deixaria o aparelho
    // sem base ate a proxima atualizacao.
    if (cabecalho_.versao != kVersao &&
        cabecalho_.versao != kVersaoSemData) {
        return ErroBase::VersaoInvalida;
    }
    if (bytes_de_dados() % kTamRegistro != 0) {
        return ErroBase::TamanhoInvalido;
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
