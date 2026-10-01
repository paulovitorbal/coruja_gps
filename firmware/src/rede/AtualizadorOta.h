#pragma once
#include <cstddef>
#include <cstdint>

#include "armazenamento/Armazenamento.h"
#include "nucleo/Configuracao.h"
#include "nucleo/VerificadorDownload.h"
#include "rede/ObservadorOta.h"
#include "nucleo/Pausa.h"
#include "rede/Baixador.h"
#include "rede/Conexao.h"

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

/// O mesmo resultado, curto o bastante para a faixa de texto da tela.
///
/// **Existe separado do `descreve()` porque os dois leitores são outros.** A
/// frase do log é lida depois, com calma, por quem abre o arquivo; a da tela
/// é lida de relance por quem está com o aparelho na mão e precisa decidir o
/// que tentar agora. "chegou integra, mas o cartao nao aceitou" explica bem
/// no log e não cabe nos 26 caracteres da faixa.
///
/// Cada falha tem texto próprio, e isso é o requisito, não um detalhe: senha
/// errada, servidor fora, sinal instável e cartão ruim se resolvem de jeitos
/// diferentes. Um "erro na atualização" genérico devolveria o usuário ao log.
const char* descreve_curto(ResultadoOta resultado);

/// Este resultado pede a atenção do usuário antes de a tela seguir adiante?
///
/// **Mora aqui, e não na composição, porque é decisão sobre o enum.** Quem
/// acrescentar um `ResultadoOta` passa por este `switch` e tem de dizer de
/// que lado ele cai; um `!= Atualizada` solto no `main.cpp` classificaria o
/// caso novo sozinho, e em silêncio.
///
/// `JaEstavaEmDia` é o que engana: nada foi baixado e ainda assim deu certo.
inline bool e_falha(ResultadoOta r) {
    switch (r) {
        case ResultadoOta::Atualizada:
        case ResultadoOta::JaEstavaEmDia:
            return false;
        case ResultadoOta::SemConfiguracao:
        case ResultadoOta::FalhaDeRede:
        case ResultadoOta::FalhaAoConsultar:
        case ResultadoOta::FalhaAoBaixar:
        case ResultadoOta::BaseRecusada:
        case ResultadoOta::FalhaAoGravar:
            return true;
    }
    return true;  // enum fora de faixa: trata como falha, que é o lado seguro
}

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
/// Orquestra a atualização da base pelo RF05: consulta a versão, baixa,
/// verifica em fluxo, grava com troca atômica.
///
/// **Recebe abstrações e não o hardware.** Foi assim que a troca atômica e as
/// três tentativas passaram a ser verificáveis no host: com `CartaoSd` e
/// `RedeWifi` concretos, 310 linhas de decisão só rodavam na placa, e provocar
/// "o cartão encheu no meio do download" na bancada é impraticável.
class AtualizadorOta {
public:
    /// `observador` e opcional: nulo significa "ninguem esta olhando", que
    /// e o caso de toda a suite de host e do modo de bancada sem painel.
    /// Nao tornar obrigatorio evitou mexer em dezenas de construcoes de
    /// teste para acrescentar um duble que elas nao usariam.
    AtualizadorOta(Armazenamento& cartao, Conexao& rede, Baixador& http,
                   Pausa& pausa, ObservadorOta* observador = nullptr)
        : cartao_(cartao), rede_(rede), http_(http), pausa_(pausa),
          observador_(observador) {}

    ResultadoOta executa(const Configuracao& cfg, Logger& log);

    const char* versao_local() const { return versao_local_; }

private:
    Armazenamento&      cartao_;
    Conexao&            rede_;
    Baixador&           http_;
    Pausa&              pausa_;
    ObservadorOta*      observador_ = nullptr;
    VerificadorDownload verificador_;
    char                versao_local_[kMaxVersao + 1] = {};
    char                versao_remota_[kMaxVersao + 1] = {};
    std::size_t         versao_remota_tam_ = 0;
};

}  // namespace coruja
