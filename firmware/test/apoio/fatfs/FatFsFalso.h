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
    /// Nomes que a enumeração deve apresentar como diretório, e não arquivo.
    std::set<std::string> diretorios;
    /// A escrita falha a partir deste byte acumulado (0 = nunca falha).
    std::size_t escrita_falha_apos = 0;
    /// A leitura falha a partir deste byte acumulado (0 = nunca falha). Sem
    /// isto, o caminho de erro do `le_em_fluxo` -- por onde a base entra no
    /// boot -- nao tem como ser exercitado.
    std::size_t leitura_falha_apos = 0;

    // --- observação
    std::vector<std::string> operacoes;   ///< "mount:2", "rename:0:/a->0:/b"
    int montagens = 0;
    int desmontagens = 0;

    /// Atalhos de leitura para os testes.
    bool existe(int volume, const std::string& nome) const;
    std::string conteudo(int volume, const std::string& nome) const;
    bool fez(const std::string& operacao) const;
};

}  // namespace coruja::teste
