#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/Configuracao.h"
#include "nucleo/VerificadorDownload.h"
#include "rede/ClienteHttp.h"
#include "rede/RedeWifi.h"

namespace coruja {

class Logger;

/// Quanto texto de versão o aparelho guarda. A resposta é uma linha qualquer
/// — data, número, hash — e não se impõe formato ao servidor; o que se impõe é
/// um teto, porque o buffer é fixo.
constexpr std::size_t kMaxVersao = 95;

enum class ResultadoOta {
    Atualizada,        ///< baixou, verificou e aceitou
    JaEstavaEmDia,     ///< a versão do servidor é a que já se tem
    SemConfiguracao,
    FalhaDeRede,
    FalhaAoConsultar,  ///< não conseguiu ler a versão do servidor
    FalhaAoBaixar,     ///< o download não completou
    BaseRecusada,      ///< baixou inteiro e o conteúdo não passou
};

const char* descreve(ResultadoOta resultado);

/// Orquestra o ciclo de atualização: conecta, consulta, baixa se preciso,
/// verifica e **desconecta sempre**.
///
/// ⚠️ **Ainda não grava nada.** O destino do `radares.bin` é o cartão, que
/// ainda não tem leitor. Enquanto isso o arquivo é verificado em fluxo e
/// descartado — o que exercita rede, formato e integridade de ponta a ponta,
/// e deixa exatamente um passo faltando. A versão confirmada também vive só
/// em RAM: reiniciar o aparelho faz o próximo clique baixar de novo.
class AtualizadorOta {
public:
    ResultadoOta executa(const Configuracao& cfg, RedeWifi& rede, Logger& log);

    const char* versao_local() const { return versao_local_; }

private:
    ClienteHttp         http_;
    VerificadorDownload verificador_;
    char                versao_local_[kMaxVersao + 1] = {};
    char                versao_remota_[kMaxVersao + 1] = {};
    std::size_t         versao_remota_tam_ = 0;
};

}  // namespace coruja
