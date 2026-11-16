#pragma once
#include <cstddef>
#include <cstdint>

#include "armazenamento/Arquivario.h"
#include "nucleo/Configuracao.h"
#include "nucleo/Pausa.h"
#include "rede/Conexao.h"
#include "rede/Enviador.h"
#include "rede/PreparoDeSessao.h"

namespace coruja {

class Logger;

/// Quantos arquivos uma remessa carrega de uma vez.
///
/// **O guarda de verdade passou a ser o tempo, não a contagem** — ver
/// `kTetoDaRemessaMs`. Este número só dimensiona a `Colheita`, o retrato do
/// cartão tirado antes do primeiro envio.
///
/// Eram doze, com a justificativa de que doze é mais do que um uso normal
/// acumula entre dois envios. Em 11/11/2026 havia **84 arquivos** esperando:
/// a doze por remessa, sete idas ao menu, cada uma reconectando, relistando e
/// terminando numa tela que pedia para ser dispensada. A contagem nunca foi o
/// que precisava de teto — o que precisa é o tempo que o aparelho fica parado.
///
/// Quarenta nomes de 32 bytes são 1280 B, membros junto com a `Bomba`: moram
/// onde o objeto morar. No `main.cpp` ele é `static`, então em `.bss`; como
/// local de `main()` estaria na pilha de 24 KiB, que é onde estava até a
/// revisão de 16/11/2026. Ver `colheita_`.
constexpr std::size_t kMaxRemessa = 40;

/// Buffer de transferência. Cada arquivo é lido duas vezes por este pedaço:
/// uma para somar o CRC, outra para mandar.
constexpr std::size_t kPedacoRemessa = 1024;

/// Teto de cada pedido — o limite de paciência, não o de operação normal:
/// quem desiste quando a rede some é `kInatividadeHttpMs`.
///
/// Os trinta segundos que estavam aqui eram um prazo de duração, e **isso
/// derrubou quatro envios seguidos em 09/10/2026**: a -88 dBm o aperto de
/// mão TLS sozinho consumia nove, e um `coruja.log` de 28 KB não cabia nos
/// vinte e um que sobravam. Na mesma sessão os arquivos de algumas centenas
/// de bytes subiam, o que fazia parecer limiar de tamanho.
///
/// O OTA já baixava 220 KB com 120 s. Pedir que o envio desse conta de 28 KB
/// em um quarto disso nunca teve justificativa.
constexpr std::uint32_t kTempoLimiteEnvioMs = 180'000;
constexpr std::uint32_t kTempoLimiteConsultaMs = 30'000;

/// Quantas vezes um arquivo é tentado antes de a remessa desistir dele.
///
/// Doze arquivos são **vinte e quatro conexões TCP**: um PUT e uma consulta
/// de CRC por arquivo, cada uma com resolução de nome, aperto de mão e
/// `Connection: close`. Sem retentativa, qualquer uma das vinte e quatro que
/// falhasse condenava o arquivo dela, e a chance de nenhuma falhar é o
/// produto de vinte e quatro chances.
///
/// É por isso que a remessa parecia falhar **"no último arquivo"**: o último
/// não tem nada de especial, é só o que teve mais sorteios antes dele.
constexpr unsigned kTentativasPorArquivo = 3;

/// Quantas vezes a confirmação é pedida antes de o arquivo contar como não
/// confirmado.
///
/// Separada da tentativa do arquivo porque o custo é outro. Quando a
/// confirmação falha, o PUT **já entrou** e o servidor já guardou; refazer o
/// arquivo inteiro reenvia o mesmo conteúdo, e é esse reenvio que duplicava
/// registro no histórico do servidor até 11/11/2026.
///
/// Repetir só a pergunta é o pedido mais barato do procedimento e evita o
/// desfecho mais caro: o arquivo ficar no cartão apesar de estar íntegro do
/// outro lado.
constexpr unsigned kTentativasDeConfirmacao = 3;

/// Quanto esperar entre duas tentativas do mesmo arquivo.
///
/// O sufixo `Remessa` no nome não é adorno: o `ObservadorOta.h` já tem um
/// `kEsperaEntreTentativasMs`, os dois vivem no mesmo `namespace coruja`, e a
/// `RemessaNaTela` inclui os dois cabeçalhos.
///
/// Menor que os 5 s do OTA de propósito: lá são três tentativas de um
/// download só, aqui podem ser dezenas de arquivos. A espera conta contra o
/// `kTetoDaRemessaMs`, como qualquer outro tempo de parede.
constexpr std::uint32_t kEsperaEntreTentativasRemessaMs = 2'000;

/// Quanto tempo a remessa inteira pode tomar do aparelho.
///
/// ⚠️ **O envio bloqueia o laço principal e não tem cancelamento.** Enquanto
/// roda, o GPS não é lido, não há alerta e o anel da UART descarta byte. Sem
/// um teto do conjunto, o pior caso era o produto dos tetos por etapa — doze
/// arquivos × (180 s de envio + 30 s de consulta) = 42 minutos — e a
/// retentativa multiplicaria isso por três.
///
/// O teto é conferido **entre arquivos e entre tentativas**, nunca no meio de
/// um pedido: abortar um PUT pela metade deixaria o servidor com meio arquivo
/// e o aparelho sem saber o que aconteceu. Ele estoura para cima em até uma
/// tentativa, portanto, e o que sobrar fica para a próxima remessa — o mesmo
/// destino de quem não coube na colheita.
constexpr std::uint32_t kTetoDaRemessaMs = 600'000;

enum class ResultadoRemessa : std::uint8_t {
    Enviada,          ///< tudo que havia foi entregue, confirmado e apagado
    NadaAEnviar,      ///< o cartão não tinha nenhum arquivo elegível
    /// O que coube foi entregue; o resto espera a próxima remessa. Não é
    /// falha: nada se perdeu e nada deu errado.
    EnviadaFaltaMais,
    /// Entregue e confirmado, mas o cartão não soltou o arquivo.
    NaoApagou,
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
        // Entregue é entregue. O que sobrou está no cartão, íntegro, e virá
        // na próxima. Fazer o usuário dispensar isso a cada remessa de um
        // backlog é atrito, não informação.
        case ResultadoRemessa::EnviadaFaltaMais:
            return false;
        // `NaoApagou` é falha, e **esta decisão mudou de lado em 11/11/2026.**
        //
        // Enquanto o `coruja.log` crescia durante a própria remessa, reter era
        // o normal e avisar seria ruído — era isso que o
        // `entregue_sem_apagar_continua_sendo_entregue` guardava. Desde que o
        // log passou a ser desviado para `remessa.log` (ver `main.cpp`), reter
        // deixou de ser normal: significa que o cartão não soltou o arquivo, e
        // que a remessa seguinte vai mandar tudo de novo.
        //
        // Calado, isso é exatamente o sintoma de "envia tudo do zero outra
        // vez" — com a tela dizendo ENVIADO.
        case ResultadoRemessa::NaoApagou:
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
    /// `indice` conta a partir de 1; `total` é quantos a remessa vai tentar;
    /// `tentativa` vai de 1 a `kTentativasPorArquivo`.
    ///
    /// A tentativa entrou pelo mesmo motivo que existe no `ObservadorOta`:
    /// sem ela, a segunda passada pelo mesmo arquivo é indistinguível da
    /// primeira na tela, e um envio que retenta parece um envio travado.
    virtual void fase(FaseRemessa fase, const char* nome, unsigned indice,
                      unsigned total, unsigned tentativa) = 0;

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
/// ⚠️ **Este parágrafo já descreveu uma retenção normal, e não descreve mais.**
/// Enquanto a remessa gravava no próprio `coruja.log`, ele subia e nunca era
/// apagado, e isso era assumido. O `main.cpp` passou a desviar o log para
/// `remessa.log` durante o envio justamente para fechar esse buraco — então
/// hoje **nenhum arquivo deveria crescer durante a remessa**, e um retido é
/// sinal de que o cartão não soltou. Ver `ResultadoRemessa::NaoApagou`.
///
/// O que a retenção custa, quando acontece: o arquivo volta inteiro na
/// remessa seguinte, e o servidor acrescenta o mesmo trecho ao histórico de
/// novo. O outro lado ganhou guarda para isso em 11/11/2026 (ver
/// `SUFIXO_MARCA`, no `servidor.py`), mas a guarda é remendo — o certo é o
/// arquivo sair do cartão.
///
/// Cada arquivo é tentado até `kTentativasPorArquivo` vezes, e a confirmação
/// até `kTentativasDeConfirmacao`, tudo dentro do `kTetoDaRemessaMs`.
class RemessaDados {
public:
    /// `pausa` é a espera **e o relógio** — as duas coisas que a retentativa
    /// e o `kTetoDaRemessaMs` precisam. Injetada pelo mesmo motivo do
    /// `AtualizadorOta`: sem isso, o teste do teto levaria dez minutos.
    ///
    /// `observador` é opcional: nulo significa "ninguém está olhando", que é o
    /// caso de quase toda a suíte de host.
    RemessaDados(Arquivario& cartao, Conexao& rede, Enviador& http,
                 Pausa& pausa, ObservadorRemessa* observador = nullptr,
                 PreparoDeSessao* preparo = nullptr)
        : cartao_(cartao), rede_(rede), http_(http), pausa_(pausa),
          observador_(observador), preparo_(preparo) {}

    ResultadoRemessa executa(const Configuracao& cfg, Logger& log);

    unsigned entregues() const { return entregues_; }
    unsigned falhados() const { return falhados_; }
    /// Entregues e confirmados, mas não apagados. Ver `NaoApagou`.
    unsigned retidos() const { return retidos_; }
    /// Elegíveis que a remessa nem chegou a tentar: não couberam na colheita
    /// ou o tempo acabou antes. Não se perderam — ficam para a próxima.
    unsigned restantes() const { return restantes_; }

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
    /// confirmado — mesmo que não tenha sido apagado.
    bool despacha(const Configuracao& cfg, const char* nome, unsigned indice,
                  unsigned total, unsigned tentativa, Logger& log);

    /// As tentativas de um arquivo, com a espera entre elas e o teto do
    /// conjunto. `true` se alguma delas chegou e foi confirmada.
    bool insiste(const Configuracao& cfg, const char* nome, unsigned indice,
                 unsigned total, Logger& log);

    /// Pergunta ao servidor o CRC do que ele guardou, repetindo a pergunta.
    ///
    /// Vale a pena repetir só esta parte: quando ela falha, o conteúdo **já
    /// está lá** e refazer o arquivo inteiro reenviaria o que já chegou.
    bool confirma(const Url& url, const Configuracao& cfg, const char* nome,
                  std::uint32_t crc_local, unsigned indice, unsigned total,
                  unsigned tentativa, Logger& log);

    /// O conjunto já tomou o tempo que podia tomar?
    ///
    /// Subtração sem sinal, como no `PrazoDeProgresso`: correta na virada dos
    /// 32 bits, que chega a cada ~49 dias de aparelho ligado.
    bool esgotou_o_tempo() const {
        return pausa_.agora_ms() - inicio_ms_ >= kTetoDaRemessaMs;
    }

    /// Avisa o observador, quando há um.
    void avisa(FaseRemessa fase, const char* nome, unsigned indice,
               unsigned total, unsigned tentativa);

    Arquivario&        cartao_;
    Conexao&           rede_;
    Enviador&          http_;
    Pausa&             pausa_;
    ObservadorRemessa* observador_ = nullptr;
    PreparoDeSessao*   preparo_ = nullptr;
    /// Membro, e não local de `despacha()`: 1 KB na pilha de 8 KB do Pico, num
    /// caminho que ainda passa pelo lwIP, é folga que não existe. Em `.bss`
    /// ele aparece no mapa de memória; na pilha, não.
    Bomba              bomba_;
    /// Membro pelo mesmo motivo da `Bomba`, e o motivo ficou mais forte: com
    /// `kMaxRemessa` em 40 são 1280 B, e isso na pilha de 8 KiB seria um
    /// sexto dela ocupado por um retrato de diretório.
    ///
    /// `executa()` zera só `n` e `ignorados`: os nomes além de `n` não são
    /// lidos por ninguém, e limpar 1280 B a cada remessa seria trabalho para
    /// não mudar nada.
    Colheita           colheita_;
    std::uint32_t      inicio_ms_ = 0;
    unsigned           entregues_ = 0;
    unsigned           falhados_ = 0;
    unsigned           retidos_ = 0;
    unsigned           restantes_ = 0;
};

}  // namespace coruja
