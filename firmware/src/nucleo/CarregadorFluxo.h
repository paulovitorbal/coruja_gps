#pragma once
#include <cstddef>
#include <cstdint>

#include "armazenamento/Armazenamento.h"
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
/// `conclui()` diz se pode. Com destino **nulo** o carregador só valida: dá o
/// mesmo veredito e a mesma contagem sem escrever nada, e é assim que a base
/// em uso é protegida na troca (ver `troca_base`).
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

/// Um pedaço lido em fluxo. A mesma forma do `CartaoSd::AoLerPedaco`.
using AoLerPedacoBase = void (*)(void* contexto, const std::uint8_t* bytes,
                                 std::size_t tamanho);

/// Quem lê um arquivo inteiro em fluxo. Na placa é o `CartaoSd::le_em_fluxo`;
/// é um ponteiro de função, e não o cartão, para que a troca rode no host
/// contra uma leitura que falha na passada que o teste escolher.
using LeituraEmFluxo = ErroCartao (*)(void* fonte, const char* nome,
                                      AoLerPedacoBase ao_ler, void* contexto,
                                      Logger& log);

/// Pontos carregados e o cabeçalho de onde vieram.
///
/// O cabeçalho sai junto porque a tela de informação mostra a **data da
/// base**, e ela vem de lá — não do momento do download.
struct BaseCarregada {
    std::size_t   pontos = 0;
    CabecalhoBase cabecalho{};
};

/// Troca a base em `destino` pela do cartão — `nome`, ou `reserva` se aquele
/// não servir — e devolve a base que passa a valer.
///
/// **A base em uso só é trocada inteira, e só depois de validada.** Não há RAM
/// para dois vetores de 281 KiB, então a troca é em dois estágios: o arquivo é
/// lido uma vez **sem destino**, só para validar (cabeçalho, CRC, ordem e
/// domínio), e só se passar é lido de novo, agora escrevendo. O custo é tempo
/// — uma segunda leitura do arquivo —, não memória.
///
/// Os três desfechos:
///  - nenhum arquivo validou: o vetor não foi tocado, e volta `em_uso`;
///  - um validou e foi copiado: volta a base nova;
///  - validou, mas a cópia falhou (cartão saindo no meio): o vetor está
///    misturado e volta **zero pontos**. Alertar sobre ele com a contagem
///    antiga seria alertar com coordenadas de duas bases.
///
/// Sem base em uso (`em_uso.pontos == 0`, o boot) não há o que proteger, e o
/// arquivo é lido uma vez só, escrevendo.
BaseCarregada troca_base(LeituraEmFluxo ler, void* fonte, const char* nome,
                         const char* reserva, Ponto* destino,
                         std::size_t capacidade, const BaseCarregada& em_uso,
                         Logger& log);

}  // namespace coruja
