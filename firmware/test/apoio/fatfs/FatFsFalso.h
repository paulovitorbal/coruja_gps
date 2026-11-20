#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ff.h"

namespace coruja::teste {

/// Estado de um volume lógico do dublê.
struct VolumeFalso {
    /// O volume está montado AGORA?
    ///
    /// ⚠️ **Isto existe porque a falta dele escondeu um defeito real.** O
    /// `f_unmount` do dublê só contava; o FatFs de verdade zera a área de
    /// trabalho do volume, e toda operação seguinte devolve `FR_NOT_ENABLED`
    /// ("The volume has no work area"). Sem modelar isso, a suíte ficou verde
    /// enquanto o aparelho perdia a base no cartão: um `log` no meio do
    /// `promove()` desmontava o volume entre os dois `f_rename`.
    bool    montado = false;
    /// O volume monta? Falso simula partição não-FAT, ausente ou ilegível.
    bool    monta = false;
    /// Que erro devolver quando não monta. FatFs distingue três causas, e o
    /// `CartaoSd` as registra separadamente de propósito — ver R-38.
    FRESULT erro_montagem = FR_NO_FILESYSTEM;
    /// Quantas vezes o volume foi montado. Um `FIL` aberto numa montagem
    /// morre na seguinte: o `f_mount` do FatFs dá à área de trabalho um
    /// identificador novo, e o `validate()` de todo objeto antigo passa a
    /// devolver `FR_INVALID_OBJECT`.
    unsigned geracao = 0;
    std::map<std::string, std::string> arquivos;
};

/// O cartão de mentira. Singleton porque a API do FatFs é C: funções livres,
/// sem contexto para carregar. Todo teste chama `reinicia()` antes.
class FatFsFalso {
public:
    static FatFsFalso& instancia();

    void reinicia();

    VolumeFalso volumes[FF_VOLUMES];

    bool driver_inicia = true;

    // --- injeção de falha, que é a razão de existir o dublê
    FRESULT erro_rename = FR_OK;
    FRESULT erro_unlink = FR_OK;
    FRESULT erro_opendir = FR_OK;
    FRESULT erro_readdir = FR_OK;
    /// O `f_close` de um arquivo ESCRITO falha com este erro, e o que foi
    /// escrito não entra. É o que o FatFs faz: o tamanho só vai ao diretório
    /// no `f_sync` de dentro do `f_close`, e se ele falha o arquivo continua
    /// com o tamanho de antes.
    FRESULT erro_close = FR_OK;
    /// Nomes que a enumeração deve apresentar como diretório, e não arquivo.
    std::set<std::string> diretorios;
    /// A escrita falha a partir deste byte acumulado (0 = nunca falha).
    std::size_t escrita_falha_apos = 0;
    /// A escrita fica CURTA a partir deste byte acumulado no arquivo aberto
    /// (0 = nunca): grava o que cabe e devolve `FR_OK` com menos bytes. É o
    /// cartão cheio, e é o caso traiçoeiro -- o retorno diz que deu certo.
    std::size_t escrita_curta_apos = 0;
    /// A leitura falha a partir deste byte acumulado (0 = nunca falha). Sem
    /// isto, o caminho de erro do `le_em_fluxo` -- por onde a base entra no
    /// boot -- nao tem como ser exercitado.
    std::size_t leitura_falha_apos = 0;

    /// A energia cai logo DEPOIS da operação com este registro em
    /// `operacoes` (vazio = nunca cai), como "rename:a->b".
    ///
    /// Daí em diante toda chamada devolve `FR_NOT_READY`, e o que estava
    /// aberto para escrita sem ter sido fechado se perde. O que já foi
    /// feito fica: um `f_open` com `FA_CREATE_ALWAYS` já truncou o arquivo,
    /// um `f_rename` já trocou o nome. É assim que se prova que uma
    /// sequência de escrita sobrevive a uma queda no meio.
    std::string queda_apos;
    /// O mesmo, contado: a energia cai depois da N-ésima operação anotada
    /// (0 = nunca). Serve para varrer TODOS os pontos de queda de uma
    /// sequência, sem o teste precisar saber quais operações ela faz.
    std::size_t queda_apos_n = 0;
    bool        sem_energia = false;
    /// A energia volta: nada montado, nada aberto, e o cartão com o que já
    /// estava gravado nele. É o boot seguinte.
    void religa();

    /// O relógio que o `CartaoSd` enxerga, em ms. O teste o adianta para
    /// atravessar a espera de remontagem.
    std::uint32_t agora_ms = 0;

    // --- observação
    /// "mount:2", "open:0:a", "write:a", "close:a", "rename:a->b", "unlink:a"
    std::vector<std::string> operacoes;
    int montagens = 0;
    int desmontagens = 0;

    /// Atalhos de leitura para os testes.
    bool existe(int volume, const std::string& nome) const;
    std::string conteudo(int volume, const std::string& nome) const;
    bool fez(const std::string& operacao) const;
};

}  // namespace coruja::teste
