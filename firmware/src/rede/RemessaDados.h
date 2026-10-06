#pragma once
#include <cstddef>
#include <cstdint>

#include "armazenamento/Arquivario.h"
#include "nucleo/Configuracao.h"
#include "rede/Conexao.h"
#include "rede/Enviador.h"

namespace coruja {

class Logger;

/// Quantos arquivos uma remessa carrega de uma vez.
///
/// O que sobrar fica para a próxima: o laço não é infinito de propósito, e o
/// aparelho não deve ficar minutos preso numa tela enquanto esvazia um cartão
/// com meses de viagem. Doze é mais do que um uso normal acumula entre dois
/// envios, e o resto não se perde — só espera.
constexpr std::size_t kMaxRemessa = 12;

/// Buffer de transferência. Cada arquivo é lido duas vezes por este pedaço:
/// uma para somar o CRC, outra para mandar.
constexpr std::size_t kPedacoRemessa = 1024;

/// Teto de cada pedido. O envio é mais generoso que a consulta porque carrega
/// o corpo inteiro; a consulta devolve oito caracteres e, se demora isso, a
/// rede não está boa o bastante para a remessa seguinte dar certo.
constexpr std::uint32_t kTempoLimiteEnvioMs = 30'000;
constexpr std::uint32_t kTempoLimiteConsultaMs = 10'000;

enum class ResultadoRemessa : std::uint8_t {
    Enviada,          ///< tudo que havia foi entregue e confirmado
    NadaAEnviar,      ///< o cartão não tinha nenhum arquivo elegível
    Parcial,          ///< alguns foram, outros não — o resto fica para depois
    SemConfiguracao,  ///< falta `url_envio`, `token_aparelho` ou rede
    FalhaDeRede,
    FalhaDeCartao,    ///< não deu nem para listar
    FalhaAoEnviar,    ///< havia o que mandar e nada completou
};

const char* descreve(ResultadoRemessa r);

/// O mesmo resultado, curto o bastante para a faixa de texto da tela.
const char* descreve_curto(ResultadoRemessa r);

/// Este resultado pede a atenção do usuário antes de a tela seguir adiante?
///
/// Mora aqui, e não na composição, pelo mesmo motivo do `e_falha(ResultadoOta)`:
/// quem acrescentar um valor ao enum passa por este `switch` e tem de dizer de
/// que lado ele cai, em vez de ser classificado em silêncio por um `!=`.
inline bool e_falha(ResultadoRemessa r) {
    switch (r) {
        case ResultadoRemessa::Enviada:
        case ResultadoRemessa::NadaAEnviar:
            return false;
        case ResultadoRemessa::Parcial:
        case ResultadoRemessa::SemConfiguracao:
        case ResultadoRemessa::FalhaDeRede:
        case ResultadoRemessa::FalhaDeCartao:
        case ResultadoRemessa::FalhaAoEnviar:
            return true;
    }
    return true;  // enum fora de faixa: trata como falha, que é o lado seguro
}

enum class FaseRemessa : std::uint8_t {
    Conectando,
    Listando,
    Enviando,
    Confirmando,  ///< perguntando o CRC ao servidor
    Apagando,
    Concluida,
    Falhou,
};

const char* descreve(FaseRemessa f);

/// Quem acompanha a remessa de fora. Mesmo papel do `ObservadorOta`: o
/// `executa()` bloqueia por dezenas de segundos e sem isto a tela congelaria.
class ObservadorRemessa {
public:
    virtual ~ObservadorRemessa() = default;

    /// `nome` é o arquivo da vez, ou vazio nas fases que não têm um.
    /// `indice` conta a partir de 1; `total` é quantos a remessa vai tentar.
    virtual void fase(FaseRemessa fase, const char* nome, unsigned indice,
                      unsigned total) = 0;

    /// Bytes de corpo já entregues e o total do arquivo da vez.
    virtual void progresso(std::size_t enviados, std::size_t total) = 0;
};

/// Este nome é um dos que o aparelho manda?
///
/// ⚠️ **A mesma lista existe no servidor**, em `PADRAO_NOME`. São dois lados
/// do mesmo contrato e precisam concordar: um nome aceito aqui e recusado lá
/// vira um arquivo que o aparelho tenta mandar para sempre, a cada remessa,
/// sem nunca conseguir.
///
///   coruja.log               o diário de diagnóstico
///   infracoes.log            o registro de passagens
///   AAAAMMDD_HHMMSS.log      um trecho de viagem
///
/// Tudo o mais fica: `coruja.cfg` tem a senha do Wi-Fi e `radares.bin` tem
/// 214 KB que o servidor acabou de mandar para cá.
bool nome_enviavel(const char* nome);

/// Manda para o servidor o que o aparelho registrou, e **apaga só o que ele
/// confirmar ter guardado**.
///
/// Por arquivo:
///
/// 1. lê inteiro para somar o CRC-32 local
/// 2. rebobina e manda, com `Content-Length` e o segredo no cabeçalho
/// 3. pergunta ao servidor o CRC do que ele guardou
/// 4. confere que o arquivo **não mudou de tamanho** desde o passo 1
/// 5. só então apaga
///
/// O passo 4 não é zelo: o `coruja.log` e o registro da viagem em curso
/// **crescem enquanto a remessa roda**. Sem ele, o aparelho mandaria N bytes,
/// receberia a confirmação de N, e apagaria um arquivo que já tinha N+k — os k
/// últimos sumiriam sem deixar rastro, que é o pior modo de falha possível
/// para um recurso cujo propósito é não perder registro.
///
/// A consequência é assumida: com `log_to_sd` ligado, o `coruja.log` chega ao
/// servidor mas não é apagado, porque a própria remessa escreve nele. Quem
/// liga o log em cartão está depurando, e limpar o arquivo é trabalho de quem
/// tira o cartão.
class RemessaDados {
public:
    /// `observador` é opcional: nulo significa "ninguém está olhando", que é o
    /// caso da suíte de host inteira.
    RemessaDados(Arquivario& cartao, Conexao& rede, Enviador& http,
                 ObservadorRemessa* observador = nullptr)
        : cartao_(cartao), rede_(rede), http_(http), observador_(observador) {}

    ResultadoRemessa executa(const Configuracao& cfg, Logger& log);

    unsigned entregues() const { return entregues_; }
    unsigned falhados() const { return falhados_; }
    /// Entregues e confirmados, mas não apagados — porque cresceram no meio.
    unsigned retidos() const { return retidos_; }

private:
    /// Guarda os nomes elegíveis durante a enumeração. A listagem não pode
    /// abrir arquivo no meio: o cartão monta e desmonta por operação.
    struct Colheita {
        char        nomes[kMaxRemessa][kMaxNomeArquivo + 1] = {};
        std::size_t n = 0;
        std::size_t ignorados = 0;  ///< elegíveis que não couberam
    };

    /// O que rola em `le()` durante o PUT.
    struct Bomba {
        Arquivario*       cartao = nullptr;
        ObservadorRemessa* observador = nullptr;
        std::size_t       entregues = 0;
        std::size_t       total = 0;
        bool              falhou = false;
        std::uint8_t      pedaco[kPedacoRemessa] = {};
    };

    static void ao_listar(void* contexto, const char* nome,
                          std::size_t tamanho);
    static std::size_t ao_pedir(void* contexto, std::uint8_t* destino,
                                std::size_t capacidade);

    /// Monta a URL do arquivo a partir de `url_envio`. `false` se não couber.
    static bool url_do_arquivo(const char* base, const char* nome, Url* destino);

    /// Um arquivo, do CRC local ao apagamento. `true` se chegou e foi
    /// confirmado — mesmo que não tenha sido apagado por ter crescido.
    bool despacha(const Configuracao& cfg, const char* nome, unsigned indice,
                  unsigned total, Logger& log);

    Arquivario&        cartao_;
    Conexao&           rede_;
    Enviador&          http_;
    ObservadorRemessa* observador_ = nullptr;
    /// Membro, e não local de `despacha()`: 1 KB na pilha de 8 KB do Pico, num
    /// caminho que ainda passa pelo lwIP, é folga que não existe. Em `.bss`
    /// ele aparece no mapa de memória; na pilha, não.
    Bomba              bomba_;
    unsigned           entregues_ = 0;
    unsigned           falhados_ = 0;
    unsigned           retidos_ = 0;
};

}  // namespace coruja
