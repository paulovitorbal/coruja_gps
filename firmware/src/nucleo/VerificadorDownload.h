#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/BaseRadares.h"
#include "nucleo/Crc32.h"

namespace coruja {

/// O cabeçalho do `radares.bin`, já decodificado. Espelha o `<4sHBBIIHBB`
/// do `formato_radares.py`, que é o contrato entre conversor, servidor e
/// firmware. Tem 16 B na versão 1 e 20 B na 2 — os quatro últimos são a data,
/// e por isso vêm DEPOIS do CRC: inserí-los no meio deslocaria o `crc32` e um
/// leitor v1 o leria do lugar errado.
struct CabecalhoBase {
    std::uint32_t magic        = 0;
    std::uint16_t versao       = 0;
    std::uint8_t  exp_escala   = 0;
    std::uint8_t  tam_registro = 0;
    std::uint32_t n_pontos     = 0;
    std::uint32_t crc          = 0;
    /// Data da base. Zero quando o arquivo e da versao 1, que nao a tinha.
    std::uint16_t ano          = 0;
    std::uint8_t  mes          = 0;
    std::uint8_t  dia          = 0;

    /// Quantos bytes o cabecalho ocupa, conforme a versao lida.
    std::size_t tamanho() const {
        return versao == kVersaoSemData ? kTamCabecalhoSemData : kTamCabecalho;
    }
    bool tem_data() const { return ano != 0; }
};

/// Decodifica o cabeçalho. Não valida nada — quem valida é
/// `VerificadorDownload::conclui()`, porque a ordem das checagens é parte do
/// contrato e precisa ficar num lugar só.
///
/// `bytes` precisa ter pelo menos `kTamCabecalhoSemData`. Os campos de data
/// só são lidos quando a versão os tem, e é o chamador quem garante que os 4
/// bytes extras chegaram — daí o `tamanho()` acima, que responde justamente
/// "quantos bytes eu preciso ter em mãos".
CabecalhoBase le_cabecalho(const std::uint8_t* bytes);

/// Verifica um `radares.bin` **enquanto ele chega**, sem nunca tê-lo inteiro.
///
/// A razão é de memória, não de elegância: o arquivo tem 214 KB e o vetor de
/// pontos já reserva 281 KB dos 520 KB da placa. Guardar o download para só
/// depois chamar `carrega_base()` não cabe — e não vai caber nem quando o
/// cartão existir, porque aí o destino é o cartão, não a RAM.
///
/// Aceita os pedaços no tamanho em que a rede os entregar, inclusive com o
/// cabeçalho partido entre dois pacotes.
///
/// ⚠️ **Não substitui o `carrega_base()`.** Aqui se verifica o que dá para
/// verificar em fluxo: cabeçalho, contagem e CRC-32. Ordenação por latitude e
/// domínio de cada registro exigem os bytes na mão e continuam sendo
/// responsabilidade da carga, quando a base for lida do cartão. Um arquivo
/// aprovado aqui ainda pode ser recusado lá, e isso é correto: esta classe
/// responde "o download chegou íntegro?", não "esta base presta?".
class VerificadorDownload {
public:
    void alimenta(const std::uint8_t* bytes, std::size_t tamanho);
    void reinicia();

    bool tem_cabecalho() const { return cabecalho_lido_; }
    /// Só faz sentido quando `tem_cabecalho()`. Antes disso vem zerado.
    const CabecalhoBase& cabecalho() const { return cabecalho_; }

    std::size_t   bytes_recebidos() const { return recebidos_; }
    std::size_t   bytes_de_dados() const {
        const std::size_t cab = cabecalho_lido_ ? cabecalho_.tamanho()
                                                : kTamCabecalho;
        return recebidos_ > cab ? recebidos_ - cab : 0;
    }
    /// CRC-32 dos bytes de dados vistos até agora (o cabeçalho fica de fora,
    /// exatamente como no `carrega_base`).
    std::uint32_t crc_calculado() const { return crc_.valor(); }

    /// Quantos pontos os bytes recebidos representam de fato.
    std::size_t pontos_recebidos() const {
        return bytes_de_dados() / kTamRegistro;
    }

    /// Veredito final, na **mesma ordem de checagens** do `carrega_base()` —
    /// de propósito: o mesmo arquivo defeituoso tem de produzir o mesmo erro
    /// nos dois caminhos, senão o log de um contradiz o do outro.
    ErroBase conclui(std::size_t capacidade) const;

private:
    Crc32         crc_;
    CabecalhoBase cabecalho_;
    std::uint8_t  buffer_cabecalho_[kTamCabecalho] = {};
    bool          cabecalho_lido_ = false;
    std::size_t   recebidos_ = 0;
};

}  // namespace coruja
