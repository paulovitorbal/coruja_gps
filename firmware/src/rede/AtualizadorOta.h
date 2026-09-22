#pragma once
#include <cstddef>
#include <cstdint>

#include "armazenamento/CartaoSd.h"
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

/// Os quatro nomes que a base ocupa no cartão. A rotação do RF05.2 usa três
/// deles; o quarto guarda a linha de versão, para que a decisão de baixar
/// sobreviva a um desligamento.
constexpr const char* kArquivoBase   = "radares.bin";
constexpr const char* kArquivoTmp    = "radares.tmp";
constexpr const char* kArquivoBak    = "radares.bak";
constexpr const char* kArquivoVersao = "versao.txt";

/// Tentativas de download, e o intervalo entre elas (RF05.2).
constexpr unsigned kTentativas = 3;
constexpr std::uint32_t kEsperaEntreTentativasMs = 5000;

enum class ResultadoOta {
    Atualizada,        ///< baixou, verificou e aceitou
    JaEstavaEmDia,     ///< a versão do servidor é a que já se tem
    SemConfiguracao,
    FalhaDeRede,
    FalhaAoConsultar,  ///< não conseguiu ler a versão do servidor
    FalhaAoBaixar,     ///< o download não completou
    BaseRecusada,      ///< baixou inteiro e o conteúdo não passou
    FalhaAoGravar,     ///< chegou íntegra e o cartão não aceitou
};

const char* descreve(ResultadoOta resultado);

/// Orquestra o ciclo de atualização: conecta, consulta, baixa se preciso,
/// verifica e **desconecta sempre**.
///
/// Grava no cartão com a **troca atômica do RF05.2**:
///
/// 1. baixa para `radares.tmp`, escrevendo enquanto os bytes chegam
/// 2. valida cabeçalho, contagem e CRC-32
/// 3. renomeia a base vigente para `radares.bak`
/// 4. renomeia `radares.tmp` para `radares.bin`
///
/// Falha em qualquer etapa **descarta o temporário e mantém a base vigente**.
/// A razão está no requisito: queda de energia durante uma atualização não
/// pode deixar o aparelho sem base, porque a tela voltaria ao velocímetro
/// normalmente e nada indicaria a perda.
///
/// A linha de versão é gravada **por último**, e só depois de a base estar no
/// lugar. Se o aparelho desligar entre os dois, a versão antiga permanece e o
/// próximo clique rebaixa — desperdício de rede, que é o modo de falha certo
/// para escolher.
class AtualizadorOta {
public:
    ResultadoOta executa(const Configuracao& cfg, CartaoSd& cartao,
                         RedeWifi& rede, Logger& log);

    const char* versao_local() const { return versao_local_; }

private:
    ClienteHttp         http_;
    VerificadorDownload verificador_;
    char                versao_local_[kMaxVersao + 1] = {};
    char                versao_remota_[kMaxVersao + 1] = {};
    std::size_t         versao_remota_tam_ = 0;
};

}  // namespace coruja
