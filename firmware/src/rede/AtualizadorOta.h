#pragma once
#include <cstddef>
#include <cstdint>

#include "armazenamento/Armazenamento.h"
#include "nucleo/CaoDeGuarda.h"
#include "nucleo/Configuracao.h"
#include "nucleo/VerificadorDownload.h"
#include "rede/ObservadorOta.h"
#include "nucleo/Pausa.h"
#include "rede/Baixador.h"
#include "rede/Conexao.h"
#include "rede/PreparoDeSessao.h"

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

/// O maior `radares.bin` que esta placa aceita: o cabeçalho mais a
/// capacidade inteira, ~281 KB.
///
/// **É o teto do que o download grava no cartão**, conferido a cada pedaço.
/// Antes dele, só o verificador recusava um arquivo grande demais — no fim,
/// com tudo já gravado: um servidor errado, ou hostil, enchia o cartão até
/// onde o cartão deixasse.
constexpr std::size_t kTetoBase =
    kTamCabecalho + std::size_t{kCapacidadeFirmware} * kTamRegistro;

enum class ResultadoOta {
    Atualizada,        ///< baixou, verificou e aceitou
    JaEstavaEmDia,     ///< a versão do servidor é a que já se tem
    SemConfiguracao,
    FalhaDeRede,
    FalhaAoConsultar,  ///< não conseguiu ler a versão do servidor
    FalhaAoBaixar,     ///< o download não completou
    BaseRecusada,      ///< baixou inteiro e o conteúdo não passou
    FalhaAoGravar,     ///< chegou íntegra e o cartão não aceitou
    /// O servidor respondeu e recusou: 3xx, 4xx. Quase sempre é o
    /// `token_aparelho` (401) ou a URL (404). Não se repete.
    ServidorRecusou,
    /// O aperto de mão TLS recusou o certificado do servidor. Quase sempre é
    /// o relógio: sem hora acertada, todo certificado "ainda não vale". Não
    /// se repete.
    CertificadoRecusado,
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
        case ResultadoOta::ServidorRecusou:
        case ResultadoOta::CertificadoRecusado:
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
    /// `preparo` e opcional: nulo significa "nada a fazer depois de
    /// conectar", que e o caso da suite de host inteira.
    /// `cao` e opcional pelo mesmo motivo: nulo significa "ninguem esta
    /// vigiando", o caso da bancada e da suite que nao mede alimentacao.
    AtualizadorOta(Armazenamento& cartao, Conexao& rede, Baixador& http,
                   Pausa& pausa, ObservadorOta* observador = nullptr,
                   PreparoDeSessao* preparo = nullptr,
                   CaoDeGuarda* cao = nullptr)
        : cartao_(cartao), rede_(rede), http_(http), pausa_(pausa),
          observador_(observador), preparo_(preparo), cao_(cao) {}

    ResultadoOta executa(const Configuracao& cfg, Logger& log);

    /// A base em uso veio do `radares.bin`? Quem sabe é a carga do boot.
    ///
    /// O `versao.txt` descreve o `radares.bin`, e só ele. Quando a carga
    /// recusa esse arquivo e cai na reserva — ou não acha base nenhuma —, a
    /// versão gravada continua igual à do servidor, e comparar só as duas
    /// respondia "já está em dia" para sempre: o menu não tinha como
    /// consertar o aparelho (F3, revisão de 17/11/2026).
    ///
    /// **É a carga que responde, e não o OTA relendo o arquivo.** Reler
    /// custaria 214 KB de cartão a cada clique em dia, e ainda responderia
    /// menos: o `VerificadorDownload` confere cabeçalho, contagem e CRC, e a
    /// carga confere também ordem e domínio — é ela que decide se o `.bin`
    /// vale ou se o aparelho cai na reserva.
    ///
    /// Sem ninguém informar, vale `false`: na dúvida o OTA rebaixa, que é o
    /// modo de falha que o resto deste fluxo já escolheu. Depois de promover
    /// uma base, o próprio OTA passa a `true` — a que ele acabou de pôr no
    /// lugar é a vigente.
    void informa_vigente_em_uso(bool sim) { vigente_em_uso_ = sim; }

    const char* versao_local() const { return versao_local_; }

private:
    // O `executa` em pedaços, na ordem em que rodam. Cada um devolve o
    // resultado em vez de encerrar: quem desconecta é só o `encerra`, e um
    // retorno adiantado lá dentro não tem como esquecer o rádio.
    void avisa(FaseOta fase, unsigned tentativa = 1);

    /// Avisa o cao de que a atualizacao ANDOU.
    ///
    /// ⚠️ So nos pontos em que algo de fato progrediu: um pedaco gravado no
    /// cartao, um pedido que voltou, uma etapa concluida. Nunca por tempo
    /// decorrido, e nunca de dentro de uma espera.
    void alimenta_o_cao() { if (cao_ != nullptr) { cao_->alimenta(); } }
    void le_versao_local(Logger& log);
    ResultadoOta encerra(ResultadoOta r, Logger& log);
    ResultadoOta fala_com_o_servidor(const Url& url_versao,
                                     const Url& url_base, Logger& log);
    /// Passo 1. `false`, com o motivo, quando a resposta não serve.
    bool consulta_versao(const Url& url, Logger& log, ResultadoOta* motivo);
    bool ja_em_dia(Logger& log) const;
    /// As tentativas do RF05.2, com a espera entre elas.
    ResultadoOta baixa_com_tentativas(const Url& url, Logger& log);
    /// Uma tentativa. `repete` diz se a seguinte pode dar outro resultado.
    ResultadoOta tenta_baixar(const Url& url, unsigned tentativa, Logger& log,
                              bool* repete);
    /// Passos 2 a 5 do RF05.2, com o download já no temporário.
    ResultadoOta verifica_e_promove(unsigned tentativa, Logger& log,
                                    bool* repete);

    Armazenamento&      cartao_;
    Conexao&            rede_;
    Baixador&           http_;
    Pausa&              pausa_;
    ObservadorOta*      observador_ = nullptr;
    PreparoDeSessao*    preparo_ = nullptr;
    CaoDeGuarda*        cao_ = nullptr;
    VerificadorDownload verificador_;
    char                versao_local_[kMaxVersao + 1] = {};
    char                versao_remota_[kMaxVersao + 1] = {};
    std::size_t         versao_remota_tam_ = 0;
    bool                vigente_em_uso_ = false;
};

}  // namespace coruja
