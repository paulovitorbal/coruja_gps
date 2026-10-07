// Ponto de entrada do **produto**.
//
// É a primeira build em que tudo que foi construído roda junto: GPS, base no
// cartão, zonamento, LED, buzzer, tela, menu de ajustes e atualização OTA.
// Até aqui cada peça tinha sua bancada; aqui elas se encontram.
//
// **Esta é a camada de composição, e ela não decide nada.** Toda a lógica
// está em classes testadas no host contra dublês — `Aplicacao`,
// `PilotoAlerta`, `MaquinaZona`, `MenuAjustes`, `AtualizadorOta` — e o que
// existe neste arquivo é a fiação: quem é construído com quem, em que ordem,
// e o que fazer quando o OTA termina. É a regra do `docs/adr/0001`.
//
// O que o usuário faz:
//
//   girar, andando   ajusta o brilho (a faixa superior mostra "BRILHO n%")
//   girar, parado    abre o menu de ajustes (RF05.1)
//   clicar, parado   atualiza a base pela rede
//   clicar, andando  recusa e avisa: "PARE PARA ATUALIZAR"
//
// **O GPS pode não estar montado**, e isso não é caso especial: sem fix o
// aparelho mostra "SEM SINAL" e suspende os alertas, que é exatamente o que
// ele deve fazer num túnel. O firmware inteiro roda assim.

#include <cstdio>
#include <initializer_list>

#include <pico/rand.h>
#include <pico/stdlib.h>

#include "app/Aplicacao.h"
#include "app/EsperaDispensa.h"
#include "VersaoBuild.h"
#include "app/OtaNaTela.h"
#include "app/EmprestimoDaBase.h"
#include "app/RemessaNaTela.h"
#include "app/PilotoAlerta.h"
#include "armazenamento/CartaoSd.h"
#include "buzzer/BuzzerGpio.h"
#include "display/Brilho.h"
#include "display/PainelSt7789.h"
#include "display/RetroiluminacaoPwm.h"
#include "display/VisorSt7789.h"
#include "encoder/EncoderKy040.h"
#include "gps/ConfiguradorGps.h"
#include "gps/LeitorGps.h"
#include "gps/UartPico.h"
#include "led/LedRgbAnodoComum.h"
#include "log/CarimboExecucao.h"
#include "log/IdExecucao.h"
#include "log/LoggerCartao.h"
#include "log/LoggerConsole.h"
#include "nucleo/BaseRadares.h"
#include "nucleo/CarimboDeLog.h"
#include "nucleo/CarregadorFluxo.h"
#include "nucleo/LeitorConfig.h"
#include "placa/PausaReal.h"
#include "rede/AtualizadorOta.h"
#include "nucleo/SincronizadorHora.h"
#include "placa/MedidorPilha.h"
#include "placa/RelogioAon.h"
#include "rede/ClienteSntp.h"
#include "rede/ClienteTls.h"
#include "rede/PlataformaMbedtls.h"
#include "rede/RemessaDados.h"
#include "rede/RedeWifi.h"

namespace {

/// A base inteira em `.bss`. Não há heap no caminho crítico, e o vetor é 83%
/// da RAM usada — medido, não estimado (`formato_dados.md` §1).
coruja::Ponto g_pontos[coruja::kCapacidadeFirmware];

/// Quanto do `coruja.cfg` cabe de uma vez.
///
/// **É o mesmo `kMaxTextoCfg` do gravador, e isso não é coincidência**: um
/// arquivo que o menu consegue reescrever tem de ser um arquivo que o boot
/// consegue ler. Eram dois números diferentes — 2048 aqui, 8192 lá —, e o
/// gerador foi passando dos 2048 sem que nada reclamasse: cada funcionalidade
/// nova acrescentava um bloco de comentário.
///
/// O sintoma, em 2026-10-06: `coruja.cfg: arquivo maior que o buffer`, o
/// aparelho sem nome na tela de informação e o OTA recusando com "sem
/// configuração utilizável" — com um arquivo que lia perfeitamente em
/// qualquer outra ferramenta.
///
/// Os comentários saíram do arquivo que vai ao cartão (ficaram no
/// `coruja.cfg.exemplo`), e os dois lados passaram a usar a MESMA constante.
constexpr std::size_t kTamBufferConfig = coruja::kMaxTextoCfg;
char g_texto_config[kTamBufferConfig];

/// Buffer da gravação cirúrgica do `coruja.cfg`. Vem de fora do
/// `GravadorConfig` de propósito: são 8 KiB, e em `.bss` de quem compõe eles
/// aparecem no mapa de memória; escondidos lá dentro, não.
char g_trabalho_cfg[coruja::kMaxTextoCfg];

constexpr const char* kArquivoLog = "coruja.log";
/// Para onde o log vai DURANTE a remessa. Não casa com o `nome_enviavel`, de
/// propósito: ele não sobe, não é apagado, e por isso sobrevive no cartão
/// para ser lido depois de um envio que falhou.
constexpr const char* kArquivoLogRemessa = "remessa.log";

void ao_ler_pedaco_da_base(void* contexto, const std::uint8_t* bytes,
                           std::size_t tamanho) {
    static_cast<coruja::CarregadorFluxo*>(contexto)->alimenta(bytes, tamanho);
}

/// Carrega a base do cartão, com o recuo do RF05.2 passo 6.
///
/// Tenta o `radares.bin`; se ele não validar, cai para o `radares.bak`. Sem
/// esse recuo, uma atualização que passasse na verificação e ainda assim
/// produzisse arquivo ruim deixaria o aparelho sem base **e sem caminho de
/// volta**, e o motorista descobriria dirigindo.
/// Pontos carregados e o cabeçalho de onde vieram.
///
/// O cabeçalho sai junto porque a tela de informação mostra a **data da
/// base**, e ela vem de lá — não do momento do download. O aparelho pode
/// ter baixado hoje uma base de três meses atrás, e é a idade dos dados que
/// diz se vale a pena atualizar.
struct BaseCarregada {
    std::size_t          pontos = 0;
    coruja::CabecalhoBase cabecalho{};
};

BaseCarregada carrega_base(coruja::CartaoSd& cartao, coruja::Logger& log) {
    coruja::CarregadorFluxo carregador(g_pontos, coruja::kCapacidadeFirmware);
    char msg[128];

    for (const char* nome : {coruja::kArquivoBase, coruja::kArquivoBak}) {
        carregador.reinicia();
        std::size_t lidos = 0;
        const auto leitura = cartao.le_em_fluxo(nome, ao_ler_pedaco_da_base,
                                                &carregador, &lidos, log);
        if (leitura != coruja::ErroCartao::Nenhum) {
            std::snprintf(msg, sizeof msg, "'%s': %s", nome, descreve(leitura));
            log.info("base", msg);
            continue;
        }
        if (carregador.conclui() != coruja::ErroBase::Nenhum) {
            std::snprintf(msg, sizeof msg, "'%s' RECUSADA", nome);
            log.error("base", msg);
            continue;
        }
        const auto& cab = carregador.cabecalho();
        if (cab.tem_data()) {
            std::snprintf(msg, sizeof msg, "%u pontos de '%s', base de %02u/%02u/%04u",
                          static_cast<unsigned>(carregador.pontos()), nome,
                          static_cast<unsigned>(cab.dia),
                          static_cast<unsigned>(cab.mes),
                          static_cast<unsigned>(cab.ano));
        } else {
            std::snprintf(msg, sizeof msg, "%u pontos de '%s' (formato antigo, sem data)",
                          static_cast<unsigned>(carregador.pontos()), nome);
        }
        log.info("base", msg);
        if (nome == coruja::kArquivoBak) {
            log.warning("base", "operando pela RESERVA: atualize quando puder");
        }
        return {carregador.pontos(), cab};
    }
    // RF07: sem base o aparelho opera, e precisa deixar isso evidente. Quem
    // avisa é a tela, com "BASE INDISPONIVEL".
    log.error("base", "NENHUMA base: nem a vigente nem a reserva");
    return {};
}

bool carrega_configuracao(coruja::CartaoSd& cartao,
                          coruja::Configuracao* destino,
                          coruja::Logger& log) {
    std::size_t lidos = 0;
    const auto erro = cartao.le_arquivo("coruja.cfg", g_texto_config,
                                        kTamBufferConfig, &lidos, log);
    if (erro != coruja::ErroCartao::Nenhum) {
        char msg[96];
        std::snprintf(msg, sizeof msg, "coruja.cfg: %s", descreve(erro));
        log.error("config", msg);
        return false;
    }
    *destino = coruja::le_config(g_texto_config, lidos, &log).config;
    return destino->ota_possivel();
}

/// O que a `Aplicacao` manda fazer e não sabe como (regra 2).
///
/// Vive aqui, na composição, porque as duas ações precisam de coisas que a
/// `Aplicacao` não tem e não deveria ter: rede, cartão e o orquestrador do
/// Reconstroi a base no vetor depois de ele ter sido emprestado ao TLS.
///
/// E o `carrega_base` de sempre, so que atras da interface que o
/// `EmprestimoDaBase` exige -- e e o que torna impossivel devolver o ponteiro
/// sem recarregar.
class RecarregadorDoCartao final : public coruja::RecarregadorBase {
public:
    explicit RecarregadorDoCartao(coruja::CartaoSd& cartao)
        : cartao_(cartao) {}

    std::size_t recarrega(coruja::Logger& log) override {
        ultima_ = carrega_base(cartao_, log);
        return ultima_.pontos;
    }

    const BaseCarregada& ultima() const { return ultima_; }

private:
    coruja::CartaoSd& cartao_;
    BaseCarregada     ultima_;
};

/// Acerta o relogio assim que a rede sobe, antes do primeiro pedido.
///
/// ⚠️ **Isto ja foi feito no lugar errado**, no construtor da
/// `SessaoDeRede`, antes de o orquestrador conectar. O `cyw43_arch_init()`
/// so acontece dentro do `RedeWifi::conecta()`, entao o cliente NTP
/// falava com a pilha de rede e com o radio antes de os dois existirem --
/// e o aparelho travava por completo no clique de atualizar: tela
/// congelada, encoder morto, sem uma linha de log.
class RelogioAntesDoTls final : public coruja::PreparoDeSessao {
public:
    RelogioAntesDoTls(coruja::SincronizadorHora& s, coruja::LeitorGps& gps)
        : sincronizador_(s), gps_(gps) {}

    /// De onde pedir a hora. Vem da configuracao RELIDA a cada acao, e
    /// nao da do boot: trocar o cartao passa a valer sem reiniciar
    /// (RNF03), e isso tem de valer para esta chave tambem.
    void define_servidor(const char* servidor) {
        std::snprintf(servidor_, sizeof servidor_, "%s",
                      servidor != nullptr ? servidor : "");
    }

    void apos_conectar(coruja::Logger& log) override {
        sincronizador_.sincroniza(servidor_, gps_.telemetria(), log);
        // Zero quando ninguem conseguiu: o mbedTLS enxerga 1970 e recusa
        // todo certificado por "ainda nao vale". E o lado seguro de nao
        // saber a data, e o log do sincronizador ja disse o porque.
        coruja::define_hora_utc(sincronizador_.hora_utc());
    }

private:
    coruja::SincronizadorHora& sincronizador_;
    coruja::LeitorGps&         gps_;
    char servidor_[coruja::kMaxUrl + 1] = {};
};

/// OTA numa; LED e buzzer na outra.
class AcoesDoAparelho final : public coruja::AcoesAplicacao {
public:
    AcoesDoAparelho(coruja::CartaoSd& cartao, coruja::AtualizadorOta& ota,
                    coruja::OtaNaTela& ponte, coruja::RemessaDados& remessa,
                    coruja::RemessaNaTela& ponte_remessa,
                    coruja::PilotoAlerta& piloto,
                    coruja::LedRgb& led, coruja::Buzzer& buzzer,
                    coruja::Pausa& pausa, coruja::Encoder& encoder,
                    coruja::LoggerCartao& log, coruja::ClienteTls& http,
                    coruja::SincronizadorHora& sincronizador,
                    RecarregadorDoCartao& recarregador,
                    coruja::LeitorGps& gps, RelogioAntesDoTls& preparo)
        : cartao_(cartao), ota_(ota), ponte_(ponte), remessa_(remessa),
          ponte_remessa_(ponte_remessa), piloto_(piloto),
          led_(led), buzzer_(buzzer), pausa_(pausa), espera_(encoder, pausa),
          log_(log), http_(http), sincronizador_(sincronizador),
          recarregador_(recarregador), gps_(gps), preparo_(preparo) {}

    /// Prepara tudo que uma sessao de rede precisa, e desfaz no fim.
    ///
    /// Isto existe porque TRES coisas tem de acontecer na ordem certa em
    /// volta de qualquer conexao TLS, e esquecer qualquer uma delas produz um
    /// defeito que nao se parece com a causa:
    ///
    /// 1. **acertar o relogio** -- sem hora, todo certificado e recusado por
    ///    "ainda nao vale", e a mensagem nao diz que o problema e a data;
    /// 2. **emprestar a memoria da base** -- o buffer de 16 KiB do mbedTLS
    ///    nao cabe no que sobra, e o vetor de radares esta ocioso;
    /// 3. **soltar o alocador e recarregar a base** no fim, inclusive quando
    ///    a sessao falha.
    ///
    /// Como classe com destrutor, e nao tres chamadas soltas, porque o passo
    /// 3 nao pode depender de alguem lembrar.
    class SessaoDeRede {
    public:
        SessaoDeRede(AcoesDoAparelho& dono, const coruja::Configuracao& cfg)
            : dono_(dono) {
            dono_.preparo_.define_servidor(cfg.servidor_ntp);
            // O relogio NAO pode ser acertado aqui: acertar exige falar com a
            // rede, e quem levanta o radio -- `cyw43_arch_init()` -- e o
            // `conecta()` la dentro do orquestrador. Ver
            // `PreparoDeSessao::apos_conectar`.
            coruja::inicia_plataforma_mbedtls();
        }

        /// Registra o quanto a pilha e a arena chegaram a usar.
        ///
        /// Os dois numeros respondem a mesma pergunta -- "o que eu reservei
        /// dava conta?" -- e os dois sao medidos, nao estimados. Sem eles, o
        /// unico jeito de descobrir que nao davam seria o aparelho travar em
        /// campo.
        void anota_consumo() {
            std::size_t usado = 0;
            std::size_t total = 0;
            char msg[96];
            if (coruja::ClienteTls::pico_do_heap(&usado, &total)) {
                std::snprintf(msg, sizeof msg,
                              "pico do heap do lwip: %u B de %u",
                              static_cast<unsigned>(usado),
                              static_cast<unsigned>(total));
                dono_.log_.info("mem", msg);
            }
            if (coruja::pico_do_heap_do_sistema(&usado, &total)) {
                std::snprintf(msg, sizeof msg,
                              "heap do sistema: %u B em uso, %u pedidos",
                              static_cast<unsigned>(usado),
                              static_cast<unsigned>(total));
                dono_.log_.info("mem", msg);
            }

            if (coruja::pico_da_pilha(&usado, &total)) {
                std::snprintf(msg, sizeof msg, "pico da pilha: %u B de %u",
                              static_cast<unsigned>(usado),
                              static_cast<unsigned>(total));
                dono_.log_.info("mem", msg);
            } else {
                // Sem tinta sobrando: a pilha foi usada ate o fundo, e o
                // pico real e DESCONHECIDO -- pode ter passado. Dizer um
                // numero aqui esconderia exatamente o transbordo.
                dono_.log_.error("mem",
                    "a pilha foi usada ate o fundo: AUMENTE PICO_STACK_SIZE");
            }
        }

        ~SessaoDeRede() {
            anota_consumo();
            // Solta a configuracao de TLS -- os tres certificados
            // interpretados -- em vez de deixa-la no heap entre sessoes. Sao
            // dezenas de KiB parados.
            dono_.http_.libera_configuracao();
        }

        SessaoDeRede(const SessaoDeRede&) = delete;
        SessaoDeRede& operator=(const SessaoDeRede&) = delete;

    private:
        AcoesDoAparelho& dono_;
    };


    /// Houve uma atualizacao desde a ultima pergunta?
    ///
    /// Consome a bandeira: quem pergunta e quem age, e agir duas vezes
    /// redesenharia a tela inteira por nada. O laco principal nao tem como
    /// saber sozinho que o OTA rodou -- ele so ve `passo()` demorar.
    bool consome_atualizacao() {
        const bool houve = houve_ota_;
        houve_ota_ = false;
        return houve;
    }

    void atualiza_base() override {
        houve_ota_ = true;
        // Descarrega o log antes: o OTA mexe no cartão por bastante tempo, e
        // se algo travar no meio o que já aconteceu fica gravado.
        log_.descarrega();

        // A configuração é relida AQUI e não guardada do boot: trocar o
        // cartão passa a valer sem reiniciar (RNF03).
        coruja::Configuracao config;
        if (!carrega_configuracao(cartao_, &config, log_)) {
            log_.error("ota", "sem configuracao utilizavel");
            mostra_falha(coruja::ResultadoOta::SemConfiguracao);
            return;
        }

        coruja::ResultadoOta resultado;
        {
            SessaoDeRede sessao(*this, config);
            resultado = ota_.executa(config, log_);
        }
        log_.descarrega();

        // **Recarrega a base so quando ela mudou.** Voltou a ser economia:
        // o emprestimo da memoria foi aposentado -- o lwIP sobrescrevia o
        // alocador e a arena nunca era usada --, entao o vetor nao e mais
        // tocado pelo TLS e nao ha o que reconstruir depois de "ja estava em
        // dia".
        if (resultado == coruja::ResultadoOta::Atualizada) {
            base_ = recarregador_.recarrega(log_) > 0 ? recarregador_.ultima()
                                                      : base_;
            piloto_.define_base(g_pontos, base_.pontos);
        }

        // **Fim feliz passa; falha espera.** Sem segurar, a tela de dirigir
        // voltaria no mesmo instante e nada seria lido. Mas o tempo que
        // basta para "ATUALIZADA", que é uma palavra só, não basta para uma
        // falha: ali o usuário precisa ler o motivo e decidir o que tentar,
        // e quem perdeu a frase não tem como pedir de novo.
        if (coruja::e_falha(resultado)) {
            mostra_falha(resultado);
        } else {
            pausa_.espera_ms(kMostraResultadoMs);
        }
    }

    /// Segura a falha na tela até o usuário dispensá-la no encoder.
    void mostra_falha(coruja::ResultadoOta resultado) {
        ponte_.falhou(coruja::descreve_curto(resultado));
        espera_.ate_dispensar(ponte_);
    }

    void envia_dados() override {
        // Descarrega ANTES: a remessa vai mandar o proprio `coruja.log`, e o
        // que ja aconteceu nesta sessao precisa estar no arquivo para subir
        // junto. Sem isto o que o servidor recebe para na ultima descarga.
        log_.descarrega();

        // Releida aqui como no OTA: trocar o cartao passa a valer sem
        // reiniciar (RNF03), e a URL de envio mora no mesmo arquivo.
        coruja::Configuracao config;
        if (!carrega_configuracao(cartao_, &config, log_)) {
            log_.error("remessa", "sem configuracao utilizavel");
            mostra_falha_remessa(coruja::ResultadoRemessa::SemConfiguracao);
            return;
        }

        // **O log e DESVIADO durante a remessa**, e nao desligado.
        //
        // Desviar e preciso porque o `coruja.log` e um dos arquivos que sobem:
        // cada linha gravada durante o envio o faz crescer, e arquivo que
        // cresceu nao e apagado (ver `RemessaDados`) -- ele subiria a cada
        // remessa e nunca sairia do cartao.
        //
        // Desligar era a solucao anterior, e ela custou caro: o envio ficou
        // CEGO. Em 07/10/2026 ele falhou tres vezes seguidas sem deixar uma
        // linha no cartao nem uma requisicao no servidor, e nao houve o que
        // diagnosticar -- so o texto de uma tela. `remessa.log` nao casa com o
        // `nome_enviavel`, entao ele nao sobe e nao e apagado: fica no cartao
        // para ser lido.
        log_.usa_arquivo(kArquivoLogRemessa);

        coruja::ResultadoRemessa resultado;
        {
            SessaoDeRede sessao(*this, config);
            resultado = remessa_.executa(config, log_);
        }

        log_.usa_arquivo(kArquivoLog);
        log_.descarrega();

        // Mesma regra do OTA: fim feliz passa, falha espera. Quem perdeu a
        // frase nao tem como pedir de novo.
        if (coruja::e_falha(resultado)) {
            mostra_falha_remessa(resultado);
        } else {
            pausa_.espera_ms(kMostraResultadoMs);
        }
    }

    void mostra_falha_remessa(coruja::ResultadoRemessa resultado) {
        ponte_remessa_.falhou(coruja::descreve_curto(resultado));
        espera_.ate_dispensar(ponte_remessa_);
    }

    void testa_alertas() override {
        // Percorre as cores do RF03.4 e dá um toque no buzzer. Existe para a
        // conferência de bancada caber no menu, sem firmware separado.
        for (const auto cor : {coruja::cores::kVerde, coruja::cores::kAmarelo,
                               coruja::cores::kRosa,
                               coruja::cores::kVermelho}) {
            led_.define_cor(cor);
            pausa_.espera_ms(400);
        }
        buzzer_.define(true);
        pausa_.espera_ms(150);
        buzzer_.define(false);
        led_.define_cor(coruja::cores::kApagado);
    }

    const BaseCarregada& base() const { return base_; }
    void define_base(const BaseCarregada& b) { base_ = b; }

private:
    static constexpr std::uint32_t kMostraResultadoMs = 2500;

    coruja::CartaoSd&       cartao_;
    coruja::AtualizadorOta& ota_;
    coruja::OtaNaTela&      ponte_;
    coruja::RemessaDados&   remessa_;
    coruja::RemessaNaTela&  ponte_remessa_;
    coruja::PilotoAlerta&   piloto_;
    coruja::LedRgb&         led_;
    coruja::Buzzer&         buzzer_;
    coruja::Pausa&          pausa_;
    coruja::EsperaDispensa  espera_;
    coruja::LoggerCartao&   log_;
    coruja::ClienteTls&     http_;
    coruja::SincronizadorHora& sincronizador_;
    RecarregadorDoCartao&      recarregador_;
    coruja::LeitorGps&         gps_;
    RelogioAntesDoTls&         preparo_;
    BaseCarregada           base_;
    bool                    houve_ota_ = false;
};

}  // namespace

int main() {
    stdio_init_all();
    sleep_ms(1500);

    // O identificador desta execucao, sorteado ANTES do primeiro log: assim
    // nenhuma linha fica orfa. Vem do TRNG do RP2350, pelo `pico_rand` -- a
    // mesma fonte da entropia do TLS.
    //
    // Ele separa as ligacoes dentro do `coruja.log`, que acumula entre elas.
    // Sem isto, so da para inferir a fronteira pelo que aparece no boot.
    coruja::define_id_execucao(get_rand_64());

    // E a HORA de cada linha, que ate agora nao existia.
    //
    // Sem ela, qualquer ferramenta de analise carimba a linha com o instante
    // em que INGERIU o arquivo -- dias depois, no caso de um log que sobe por
    // remessa. Enquanto o NTP nao acerta o relogio sai o tempo desde o boot,
    // com `+`, que nao se confunde com uma data (ver `formata_carimbo`).
    coruja::define_fonte_de_carimbo([]() -> const char* {
        static char carimbo[coruja::kTamCarimbo];
        coruja::formata_carimbo(coruja::hora_utc(),
                                to_ms_since_boot(get_absolute_time()),
                                carimbo, sizeof carimbo);
        return carimbo;
    });

    // Pinta a pilha livre ANTES de qualquer trabalho. O handshake do mbedTLS
    // e o que mais consome aqui, e o tamanho reservado (16 KiB) foi escolhido
    // com folga porque ninguem o mediu -- isto e o que troca o palpite por
    // um numero.
    coruja::pinta_pilha();

    coruja::LoggerConsole console(nullptr, coruja::Nivel::Info);
    coruja::CartaoSd      cartao;
    coruja::LoggerCartao  log(console, cartao, kArquivoLog);

    // A tela primeiro, antes de qualquer coisa que possa falhar: é o único
    // canal capaz de contar o que deu errado. Tela preta durante o boot
    // parece aparelho morto.
    coruja::RetroiluminacaoPwm luz;
    coruja::PainelSt7789       painel;
    painel.inicia();
    coruja::VisorSt7789 visor{painel};

    coruja::LedRgbAnodoComum led;
    coruja::BuzzerGpio       buzzer;
    coruja::EncoderKy040     encoder;
    coruja::Brilho           brilho;
    coruja::PausaReal        pausa;
    coruja::UartPico         uart;
    coruja::LeitorGps        gps(uart);
    coruja::RedeWifi         rede;
    coruja::ClienteTls       http;
    coruja::ClienteSntp      ntp;
    coruja::RelogioAon       relogio;

    log.info("boot", "coruja gps");
    cartao.inicia(log);

    coruja::Configuracao config;
    carrega_configuracao(cartao, &config, log);
    log.define_nivel_minimo(config.nivel_log);
    log.grava_no_cartao(config.log_para_cartao);
    brilho.define_presets(config.brilho_dia, config.brilho_noite);
    luz.define_duty(brilho.duty());

    // O GPS a 4 Hz e 115200: sem isto ele fala a 1 Hz e 9600, e o RF01.4
    // exige 4 Hz. Falhar aqui não impede o aparelho de operar — só o deixa
    // mais lento, e o monitor de taxa vai dizer isso na tela.
    coruja::ConfiguradorGps configurador(uart, pausa);
    configurador.executa(log);

    coruja::PilotoAlerta piloto(gps, led, buzzer);
    const BaseCarregada base = carrega_base(cartao, log);
    piloto.define_base(g_pontos, base.pontos);

    // O relogio comeca ZERADO: o RP2350 nao tem bateria nele, e zero e
    // exatamente o que se quer dizer -- ninguem acertou a hora nesta ligacao.
    relogio.inicia();
    if (!http.define_token(config.token_aparelho)) {
        // Sem token o servidor recusa com 401, e o log diz por que. Seguir com
        // um token que traz `\r\n` seria mandar cabecalho que ninguem
        // escreveu de proposito.
        log.error("cfg", "token_aparelho tem caractere invalido: ignorado");
    }

    coruja::SincronizadorHora sincronizador(relogio, ntp);
    RecarregadorDoCartao      recarregador(cartao);
    // O relogio e acertado pelos ORQUESTRADORES, logo depois de a rede
    // subir -- nunca antes. Ver `PreparoDeSessao`.
    RelogioAntesDoTls         preparo(sincronizador, gps);

    coruja::OtaNaTela      ponte{visor, led, pausa};
    coruja::AtualizadorOta ota(cartao, rede, http, pausa, &ponte, &preparo);
    coruja::RemessaNaTela  ponte_remessa{visor, led, pausa};
    coruja::RemessaDados   remessa(cartao, rede, http, &ponte_remessa,
                                   &preparo);
    AcoesDoAparelho        acoes(cartao, ota, ponte, remessa, ponte_remessa,
                                 piloto, led, buzzer, pausa, encoder, log,
                                 http, sincronizador, recarregador, gps,
                                 preparo);
    acoes.define_base(base);

    coruja::Aplicacao app(gps, encoder, piloto, brilho, cartao, acoes, log,
                          config, g_trabalho_cfg, sizeof g_trabalho_cfg,
                          &visor);
    app.define_versao(coruja::kVersaoBuild);
    app.define_base_carregada(base.cabecalho, base.pontos);

    log.info("boot", "pronto");
    log.descarrega();

    while (true) {
        app.passo(pausa.agora_ms());

        // Depois de um OTA, a tela dele ficou por cima: as telas só
        // redesenham o que mudou, e o que a atualização deixou no painel não
        // está em nenhum instantâneo. A bandeira vem de quem executou o
        // OTA — o laço só vê `passo()` demorar, e não teria como adivinhar.
        if (acoes.consome_atualizacao()) {
            app.invalida_tela();
            app.define_base_carregada(acoes.base().cabecalho,
                                      acoes.base().pontos);
        }
    }
}
