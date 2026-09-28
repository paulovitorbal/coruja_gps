#pragma once
#include <map>
#include <string>
#include <vector>

#include "ff.h"

namespace coruja::teste {

/// Estado de um volume lógico do dublê.
struct VolumeFalso {
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
    /// A escrita falha a partir deste byte acumulado (0 = nunca falha).
    std::size_t escrita_falha_apos = 0;

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
