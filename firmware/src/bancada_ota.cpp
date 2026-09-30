// Bancada de atualização: o OTA de ponta a ponta, na tela e no LED.
//
// **É a primeira build em que a atualização mostra o que está fazendo.** Até
// aqui o OTA existia e funcionava, mas só falava com o console USB — quem
// estivesse olhando o aparelho, e não o computador, veria o LED apagar e
// nada mais.
//
//   clicar   inicia a atualização: conecta, consulta, baixa, verifica, grava
//   girar    ajusta o brilho, como no produto com o carro andando
//
// O que roda é o que vai para o produto: `AtualizadorOta`, `TelaOta`,
// `PadraoLedOta` e `OtaNaTela`, mais `CartaoSd`, `RedeWifi` e `ClienteHttp`.
// Não há GPS — ele ainda não foi montado, e a atualização não precisa dele.
//
// Entre cliques a tela mostra a última fase e o LED a acompanha, então o
// resultado fica legível sem console: verde fixo deu certo, vermelho
// piscando falhou.

#include <cstdio>

#include <pico/stdio_usb.h>
#include <pico/stdlib.h>

#include "app/OtaNaTela.h"
#include "armazenamento/CartaoSd.h"
#include "display/Brilho.h"
#include "display/PainelSt7789.h"
#include "display/RetroiluminacaoPwm.h"
#include "display/VisorSt7789.h"
#include "encoder/EncoderKy040.h"
#include "led/LedRgbAnodoComum.h"
#include "log/LoggerCartao.h"
#include "log/LoggerConsole.h"
#include "nucleo/LeitorConfig.h"
#include "placa/PausaReal.h"
#include "rede/AtualizadorOta.h"
#include "rede/ClienteHttp.h"
#include "rede/RedeWifi.h"

namespace {

/// Espaço para o `coruja.cfg`, em `.bss` como todo o resto: não há heap no
/// caminho crítico.
constexpr std::size_t kTamBufferConfig = 2048;
char g_texto_config[kTamBufferConfig];

constexpr const char* kArquivoLog = "coruja.log";

/// Lê a configuração do cartão **na hora do clique**, não no boot.
///
/// O RNF03 é explícito: as credenciais não podem estar embutidas no
/// firmware. Ler no clique também significa que trocar o cartão passa a
/// valer sem reiniciar, e que a configuração nunca fica velha em RAM.
bool carrega_configuracao(coruja::CartaoSd& cartao,
                          coruja::Configuracao* destino,
                          coruja::Logger& log) {
    std::size_t lidos = 0;
    const auto erro = cartao.le_arquivo("coruja.cfg", g_texto_config,
                                        kTamBufferConfig, &lidos, log);
    char msg[128];
    if (erro != coruja::ErroCartao::Nenhum) {
        std::snprintf(msg, sizeof msg, "coruja.cfg: %s", descreve(erro));
        log.error("config", msg);
        return false;
    }
    const auto lido = coruja::le_config(g_texto_config, lidos, &log);
    *destino = lido.config;
    std::snprintf(msg, sizeof msg, "config: %u rede(s), urls %s, unidade '%s'",
                  static_cast<unsigned>(lido.config.n_redes),
                  lido.config.tem_urls() ? "ok" : "AUSENTES",
                  lido.config.nome);
    log.info("config", msg);
    return lido.config.ota_possivel();
}

}  // namespace

int main() {
    stdio_init_all();
    for (int i = 0; i < 300 && !stdio_usb_connected(); ++i) {
        sleep_ms(100);
    }
    sleep_ms(300);

    coruja::LoggerConsole console(nullptr, coruja::Nivel::Debug);
    coruja::CartaoSd      cartao;
    coruja::LoggerCartao  log(console, cartao, kArquivoLog);

    coruja::RetroiluminacaoPwm luz;
    coruja::PainelSt7789       painel;
    coruja::LedRgbAnodoComum   led;
    coruja::EncoderKy040       encoder;
    coruja::Brilho             brilho;
    coruja::RedeWifi           rede;
    coruja::ClienteHttp        http;
    coruja::PausaReal          pausa;

    log.info("boot", "coruja - bancada de atualizacao (OTA na tela)");
    cartao.inicia(log);

    painel.inicia();
    coruja::VisorSt7789 visor{painel};
    coruja::OtaNaTela   ponte{visor, led, pausa};
    coruja::AtualizadorOta ota(cartao, rede, http, pausa, &ponte);

    // A configuração é lida aqui só para as preferências de log e o brilho
    // gravado; o que vale para o Wi-Fi é relido a cada clique.
    {
        coruja::Configuracao inicial;
        carrega_configuracao(cartao, &inicial, log);
        log.define_nivel_minimo(inicial.nivel_log);
        log.grava_no_cartao(inicial.log_para_cartao);
        brilho.define_presets(inicial.brilho_dia, inicial.brilho_noite);
    }
    luz.define_duty(brilho.duty());

    // Estado inicial da tela: a fase de conexão, ainda apagada. Dá ao
    // usuário algo coerente para olhar antes do primeiro clique, em vez de
    // tela preta que parece aparelho morto.
    ponte.mantem();
    log.info("ihm", "clicar = atualizar  |  girar = brilho");

    while (true) {
        const auto evento = encoder.proximo_evento();

        if (evento == coruja::EventoEncoder::Clique) {
            // Descarrega o log antes: o OTA vai mexer no cartão por bastante
            // tempo, e se algo travar no meio o que já aconteceu fica
            // gravado.
            log.descarrega();

            coruja::Configuracao config;
            if (carrega_configuracao(cartao, &config, log)) {
                ota.executa(config, log);
            } else {
                log.error("ota", "sem configuracao utilizavel: nada a fazer");
                ponte.fase(coruja::FaseOta::Falhou, 1);
            }
            log.define_nivel_minimo(config.nivel_log);
            log.grava_no_cartao(config.log_para_cartao);
            log.descarrega();
        } else if (evento == coruja::EventoEncoder::GiroDireita) {
            brilho.aumenta();
            luz.define_duty(brilho.duty());
        } else if (evento == coruja::EventoEncoder::GiroEsquerda) {
            brilho.diminui();
            luz.define_duty(brilho.duty());
        }

        // Fora do OTA o LED continua piscando conforme a última fase: o
        // resultado fica legível sem console, e é o que permite deixar o
        // aparelho atualizando e voltar depois.
        ponte.mantem();
        sleep_ms(1);  // a quadratura precisa ver cada transicao
    }
}
