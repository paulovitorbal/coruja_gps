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

/// Grava os ajustes no `coruja.cfg` sem regerar o arquivo.
///
/// Le, reescreve so os valores das cinco chaves (EscritorConfig) e troca o
/// arquivo de forma atomica: escreve o `coruja.tmp` inteiro, e so entao o
/// promove. Uma queda de energia no meio da escrita deixa o `coruja.cfg`
/// antigo intacto -- que importa mais aqui do que na base de radares,
/// porque sem o cfg o aparelho perde as redes e nao consegue nem se
/// atualizar para se consertar.
///
/// **Nao inventa arquivo.** Se nao ha `coruja.cfg`, recusa em vez de criar
/// um so com os ajustes: seria um arquivo sem redes nem URLs, e o aparelho
/// passaria a parecer configurado quando o cartao e que esta errado.
///
/// O buffer de trabalho vem de fora porque sao 8 KB: em `.bss` de quem
/// chama, ele aparece no mapa de memoria; escondido aqui dentro, nao.
ResultadoGravacao grava_ajustes(Armazenamento& cartao, const Configuracao& cfg,
                                char* trabalho, std::size_t capacidade,
                                Logger& log);

}  // namespace coruja
