#pragma once
#include <cstddef>
#include <cstdint>

#include "armazenamento/Armazenamento.h"

namespace coruja {

class Logger;

/// Maior nome de arquivo que a remessa carrega. O maior que o firmware gera é
/// `AAAAMMDD_HHMMSS.log`, com 19; 31 deixa folga sem pesar na pilha.
constexpr std::size_t kMaxNomeArquivo = 31;

/// O que a remessa de dados precisa do cartão: **enumerar, ler e apagar**.
///
/// É um segundo recorte do `CartaoSd`, irmão do `Armazenamento` — e não um
/// acréscimo a ele, de propósito. O `Armazenamento` é o recorte que o OTA usa:
/// escrever em fluxo e promover. Juntar os dois obrigaria os cinco dublês de
/// teste já existentes a ganhar métodos que nenhum deles chama, só para
/// continuarem compilando.
///
/// **A listagem é por retorno de chamada, sem vetor.** Quantos arquivos há no
/// cartão é decisão de quem o enche; reservar um vetor para "o máximo
/// imaginável" seria `.bss` desperdiçado no caso comum e insuficiente no raro.
///
/// **A leitura é sob demanda, e não em fluxo empurrado.** O `le_em_fluxo()` do
/// cartão entrega pedaços quando quer; a pilha de rede pede pedaços quando a
/// janela TCP abre. Casar os dois exigiria guardar o arquivo inteiro no meio —
/// que é justamente o que não cabe. Por isso o trio abre/lê/fecha, igual ao da
/// escrita em `Armazenamento`, e não um `le_em_fluxo` com retorno de chamada.
class Arquivario {
public:
    /// Um arquivo encontrado na raiz do volume. `nome` **só vale durante a
    /// chamada** — quem precisar dele depois tem de copiar.
    using AoListar = void (*)(void* contexto, const char* nome,
                              std::size_t tamanho);

    virtual ~Arquivario() = default;

    /// Enumera os arquivos da raiz. Diretórios são ignorados: o firmware não
    /// cria nenhum, e descer em árvore só traria caminhos que nenhuma outra
    /// parte do projeto sabe nomear.
    virtual ErroCartao lista(AoListar ao_listar, void* contexto,
                             Logger& log) = 0;

    /// Monta e abre `nome` para leitura, deixando o volume montado. Preenche
    /// `tamanho` com o total de bytes — a remessa precisa dele antes do
    /// primeiro byte, para o `Content-Length`.
    virtual ErroCartao abre_para_leitura(const char* nome,
                                         std::size_t* tamanho, Logger& log) = 0;

    /// Lê até `capacidade` bytes. `lidos` menor que `capacidade` significa fim
    /// de arquivo. Devolve `false` em falha de leitura — e aí quem chama tem
    /// de abortar, porque um envio com buraco no meio ainda assim chegaria ao
    /// servidor com `Content-Length` satisfeito.
    virtual bool le(std::uint8_t* destino, std::size_t capacidade,
                    std::size_t* lidos) = 0;

    /// Volta ao primeiro byte do arquivo aberto.
    ///
    /// A remessa lê cada arquivo **duas vezes**: uma para somar o CRC-32 e
    /// outra para mandar. Reabrir entre as duas remontaria o volume; e guardar
    /// o conteúdo da primeira passada para reaproveitar na segunda é o que não
    /// cabe em RAM.
    virtual bool rebobina() = 0;

    /// Fecha o arquivo aberto e desmonta. Idempotente.
    virtual void fecha_leitura() = 0;

    /// Apaga `nome`. **Só é chamado depois de o servidor confirmar o CRC** —
    /// ver `RemessaDados`. Arquivo ausente é `ArquivoAusente`, e não sucesso:
    /// quem mandou apagar acreditava que ele estava lá, e a diferença entre
    /// "apaguei" e "não havia nada" importa no log.
    virtual ErroCartao remove(const char* nome, Logger& log) = 0;
};

}  // namespace coruja
