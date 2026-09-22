#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/BaseRadares.h"
#include "nucleo/Ponto.h"
#include "nucleo/VerificadorDownload.h"

namespace coruja {

class Logger;

/// Carrega o `radares.bin` **enquanto ele é lido**, decodificando direto no
/// vetor de destino.
///
/// Existe por aritmética, não por elegância: o arquivo tem 214 KB e o vetor
/// decodificado ocupa 281 KB. Juntos passam de meio megabyte, contra ~185 KiB
/// livres. Não há como ler inteiro para depois chamar `carrega_base()` — os
/// bytes têm de virar `Ponto` na passagem.
///
/// Dá **o mesmo veredito** que o `carrega_base()`, na mesma ordem de
/// checagens. Há teste que compara os dois sobre o mesmo arquivo, íntegro e
/// corrompido de várias formas: se divergirem, um dos dois está errado.
///
/// O destino é escrito **durante** o fluxo, antes de o CRC ser conhecido. Em
/// caso de recusa o conteúdo dele fica indefinido e não deve ser usado —
/// `conclui()` diz se pode.
class CarregadorFluxo {
public:
    CarregadorFluxo(Ponto* destino, std::size_t capacidade);

    void alimenta(const std::uint8_t* bytes, std::size_t tamanho);
    void reinicia();

    /// Veredito final. Mesma ordem do `carrega_base()`: primeiro o que o
    /// cabeçalho e o CRC dizem, e só depois ordenação e domínio dos registros
    /// — que o fluxo detecta antes, mas que não podem "furar a fila" sob pena
    /// de os dois caminhos darem erros diferentes para o mesmo arquivo.
    ErroBase conclui() const;

    std::size_t pontos() const { return escritos_; }
    const CabecalhoBase& cabecalho() const { return verificador_.cabecalho(); }
    std::size_t bytes_recebidos() const { return verificador_.bytes_recebidos(); }

private:
    void consome_registro(const std::uint8_t* registro);

    VerificadorDownload verificador_;
    Ponto*       destino_;
    std::size_t  capacidade_;
    std::size_t  escritos_ = 0;
    /// Um registro pode chegar partido entre dois pedaços de leitura: 12 não
    /// divide 4096, e menos ainda o que a rede entrega.
    std::uint8_t parcial_[kTamRegistro] = {};
    std::size_t  parcial_n_ = 0;
    std::int32_t lat_anterior_ = -2147483647 - 1;
    ErroBase     erro_de_registro_ = ErroBase::Nenhum;
};

}  // namespace coruja
