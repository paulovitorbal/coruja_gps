#pragma once
#include <cstddef>

#include "armazenamento/Armazenamento.h"
#include "nucleo/Configuracao.h"

namespace coruja {

class Logger;

/// Os tres nomes da rotacao do coruja.cfg, na mesma forma que a base usa.
constexpr const char* kArquivoCfg    = "coruja.cfg";
constexpr const char* kArquivoCfgTmp = "coruja.tmp";
constexpr const char* kArquivoCfgBak = "coruja.bak";

/// Quanto do arquivo cabe em RAM de uma vez. O gerado tem ~2,5 KB; 8 KB
/// deixa folga para comentarios de quem editar a mao, e recusar o que passa
/// disso e melhor que gravar um arquivo truncado por cima do bom.
constexpr std::size_t kMaxTextoCfg = 8192;

enum class ResultadoGravacao {
    Gravado,
    SemCartao,
    ArquivoAusente,   ///< nao ha coruja.cfg para alterar
    GrandeDemais,     ///< nao cabe em kMaxTextoCfg
    FalhaDeEscrita,
    FalhaDeTroca,     ///< escreveu o .tmp, mas a troca atomica nao completou
};

const char* descreve(ResultadoGravacao r);

/// Lê o texto do `coruja.cfg`, ou o do `coruja.bak` quando o cfg **não está
/// no cartão**.
///
/// A troca atômica (`promove`) faz cfg -> bak e só depois tmp -> cfg. Uma
/// queda entre os dois renomeios deixa o cartão sem `coruja.cfg`, com o
/// arquivo anterior inteiro na reserva — e quem lesse só o cfg perderia as
/// redes e as URLs para sempre. É o mesmo recuo que a base de radares faz.
///
/// **Só a ausência** cai para a reserva. Um cfg grande demais ou ilegível
/// está no cartão e é o que vale: reescrever ajustes em cima da reserva
/// nesse caso desfaria o arquivo que alguém editou à mão.
ErroCartao le_texto_config(Armazenamento& cartao, char* destino,
                           std::size_t capacidade, std::size_t* lidos,
                           Logger& log);

/// Lê e interpreta a configuração, com o mesmo recuo de `le_texto_config`.
/// Devolve se o OTA é possível com ela.
///
/// É **a** leitura do `coruja.cfg` de todo alvo: o produto e a bancada de
/// OTA tinham cada um a sua, e a da bancada ficou com o buffer de 2 KiB
/// depois que o produto já tinha passado a 8 (ver `kMaxTextoCfg`). O texto
/// vem para `buffer` porque ele é grande, e em `.bss` de quem compõe
/// aparece no mapa de memória.
bool carrega_configuracao(Armazenamento& cartao, char* buffer,
                          std::size_t capacidade, Configuracao* destino,
                          Logger& log);

/// Grava os ajustes no `coruja.cfg` sem regerar o arquivo.
///
/// Le, reescreve so os valores das cinco chaves (EscritorConfig) e troca o
/// arquivo de forma atomica: escreve o `coruja.tmp` inteiro, e so entao o
/// promove. Uma queda de energia no meio da escrita deixa o `coruja.cfg`
/// antigo intacto -- que importa mais aqui do que na base de radares,
/// porque sem o cfg o aparelho perde as redes e nao consegue nem se
/// atualizar para se consertar.
///
/// **Nao inventa arquivo.** Se nao ha `coruja.cfg` nem reserva, recusa em vez
/// de criar um so com os ajustes: seria um arquivo sem redes nem URLs, e o
/// aparelho passaria a parecer configurado quando o cartao e que esta
/// errado. Com so a reserva -- a queda no meio da troca --, parte dela, e a
/// promocao devolve o `coruja.cfg` ao cartao.
///
/// O buffer de trabalho vem de fora porque sao 8 KB: em `.bss` de quem
/// chama, ele aparece no mapa de memoria; escondido aqui dentro, nao.
ResultadoGravacao grava_ajustes(Armazenamento& cartao, const Configuracao& cfg,
                                char* trabalho, std::size_t capacidade,
                                Logger& log);

}  // namespace coruja
