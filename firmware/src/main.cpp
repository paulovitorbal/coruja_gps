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

#include <pico/stdlib.h>

#include "app/Aplicacao.h"
#include "app/EsperaDispensa.h"
#include "VersaoBuild.h"
#include "app/OtaNaTela.h"
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
#include "log/LoggerCartao.h"
#include "log/LoggerConsole.h"
#include "nucleo/BaseRadares.h"
#include "nucleo/CarregadorFluxo.h"
#include "nucleo/LeitorConfig.h"
#include "placa/PausaReal.h"
#include "rede/AtualizadorOta.h"
#include "rede/ClienteHttp.h"
#include "rede/RedeWifi.h"

namespace {

/// A base inteira em `.bss`. Não há heap no caminho crítico, e o vetor é 83%
/// da RAM usada — medido, não estimado (`formato_dados.md` §1).
coruja::Ponto g_pontos[coruja::kCapacidadeFirmware];

constexpr std::size_t kTamBufferConfig = 2048;
char g_texto_config[kTamBufferConfig];

/// Buffer da gravação cirúrgica do `coruja.cfg`. Vem de fora do
/// `GravadorConfig` de propósito: são 8 KiB, e em `.bss` de quem compõe eles
/// aparecem no mapa de memória; escondidos lá dentro, não.
char g_trabalho_cfg[coruja::kMaxTextoCfg];

constexpr const char* kArquivoLog = "coruja.log";

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
/// OTA numa; LED e buzzer na outra.
class AcoesDoAparelho final : public coruja::AcoesAplicacao {
public:
    AcoesDoAparelho(coruja::CartaoSd& cartao, coruja::AtualizadorOta& ota,
                    coruja::OtaNaTela& ponte, coruja::PilotoAlerta& piloto,
                    coruja::LedRgb& led, coruja::Buzzer& buzzer,
                    coruja::Pausa& pausa, coruja::Encoder& encoder,
                    coruja::LoggerCartao& log)
        : cartao_(cartao), ota_(ota), ponte_(ponte), piloto_(piloto),
          led_(led), buzzer_(buzzer), pausa_(pausa), espera_(encoder, pausa),
          log_(log) {}

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

        const auto resultado = ota_.executa(config, log_);
        log_.descarrega();

        // **Recarrega a base só quando ela mudou.** Recarregar sempre custaria
        // ~1 s de leitura de cartão por clique, e depois de "já estava em dia"
        // não há nada de novo para ler.
        if (resultado == coruja::ResultadoOta::Atualizada) {
            base_ = carrega_base(cartao_, log_);
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
    coruja::PilotoAlerta&   piloto_;
    coruja::LedRgb&         led_;
    coruja::Buzzer&         buzzer_;
    coruja::Pausa&          pausa_;
    coruja::EsperaDispensa  espera_;
    coruja::LoggerCartao&   log_;
    BaseCarregada           base_;
    bool                    houve_ota_ = false;
};

}  // namespace

int main() {
    stdio_init_all();
    sleep_ms(1500);

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
    coruja::ClienteHttp      http;

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

    coruja::OtaNaTela      ponte{visor, led, pausa};
    coruja::AtualizadorOta ota(cartao, rede, http, pausa, &ponte);
    AcoesDoAparelho        acoes(cartao, ota, ponte, piloto, led, buzzer,
                                 pausa, encoder, log);
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
