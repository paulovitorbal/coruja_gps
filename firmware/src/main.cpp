// Ponto de entrada. Hoje roda o MODO DE BANCADA DE REDE, que exercita o
// caminho de atualização OTA de ponta a ponta:
//
//   girar   -> percorre as quatro cores de estado de via (RF03.4)
//              direita agrava, esquerda alivia; satura nas pontas
//   clicar  -> conecta no Wi-Fi, consulta a versão, baixa se for diferente,
//              verifica, imprime o cabeçalho e DESCONECTA
//
// Tudo sai no console USB: as redes vistas na varredura, o IP obtido, o GET,
// os seis campos do cabeçalho do `radares.bin` e o que de fato chegou.
//
// O que ainda falta, e está avisado em tempo de execução: nada é gravado. O
// destino do arquivo é o cartão, e o leitor de cartão ainda não existe. A
// versão confirmada vive em RAM, então reiniciar faz o próximo clique baixar
// de novo — que na bancada é o que se quer.
//
// Os modos anteriores continuam no projeto e testados: `ModoCalibracao` (que
// fechou o R-05) e `ModoTesteEncoder`.
#include <pico/stdlib.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "app/CicloCores.h"
#include "encoder/EncoderKy040.h"
#include "led/LedRgbAnodoComum.h"
#include "log/LoggerCartao.h"
#include "log/LoggerConsole.h"
#include "armazenamento/CartaoSd.h"
#include "nucleo/BaseRadares.h"
#include "nucleo/CarregadorFluxo.h"
#include "nucleo/LeitorConfig.h"
#include "rede/AtualizadorOta.h"
#include "rede/RedeWifi.h"

namespace {

coruja::Ponto g_pontos[coruja::kCapacidadeFirmware];

/// Período de amostragem do encoder. Ver a justificativa no ADR 0005: a 4 Hz
/// do laço de navegação um giro rápido perderia estados.
constexpr std::uint32_t kPeriodoAmostragemMs = 1;

/// Espaço para o `coruja.cfg` lido do cartão. Em `.bss`, como todo o resto:
/// não há heap no caminho crítico. 2 KiB cobre com folga o arquivo que o
/// `gera_config.py` produz (985 B hoje, teto de ~1,2 KiB com cinco redes).
constexpr std::size_t kTamBufferConfig = 2048;

/// Onde o log vai parar quando `log_to_sd=true`. Em modo de acréscimo, então
/// ele atravessa reinicializações — o que é o ponto: um travamento que o
/// console não pegou fica gravado para a próxima vez.
constexpr const char* kArquivoLog = "coruja.log";
char g_texto_config[kTamBufferConfig];

void ao_ler_pedaco_da_base(void* contexto, const std::uint8_t* bytes,
                           std::size_t tamanho) {
    static_cast<coruja::CarregadorFluxo*>(contexto)->alimenta(bytes, tamanho);
}

/// Carrega a base do cartão, com o recuo do RF05.2 passo 6.
///
/// Tenta o `radares.bin`. Se ele não validar — corrompido, truncado, ou de um
/// formato que este firmware não aceita — cai para o `radares.bak`, que é a
/// base que funcionava antes da última atualização.
///
/// Sem esse recuo, uma atualização que passasse na verificação e mesmo assim
/// produzisse um arquivo ruim deixaria o aparelho sem base **e sem caminho de
/// volta**, e o motorista descobriria dirigindo.
std::size_t carrega_base_do_cartao(coruja::CartaoSd& cartao,
                                   coruja::Logger& log) {
    coruja::CarregadorFluxo carregador(g_pontos, coruja::kCapacidadeFirmware);
    char msg[128];

    for (const char* nome : {coruja::kArquivoBase, coruja::kArquivoBak}) {
        carregador.reinicia();
        std::size_t lidos = 0;
        const auto leitura = cartao.le_em_fluxo(nome, ao_ler_pedaco_da_base,
                                                &carregador, &lidos, log);
        if (leitura == coruja::ErroCartao::ArquivoAusente) {
            std::snprintf(msg, sizeof msg, "'%s' nao existe no cartao", nome);
            log.info("base", msg);
            continue;
        }
        if (leitura != coruja::ErroCartao::Nenhum) {
            std::snprintf(msg, sizeof msg, "'%s': %s", nome, descreve(leitura));
            log.error("base", msg);
            continue;
        }

        const auto veredito = carregador.conclui();
        if (veredito != coruja::ErroBase::Nenhum) {
            std::snprintf(msg, sizeof msg, "'%s' RECUSADA: %s", nome,
                          descreve(veredito));
            log.error("base", msg);
            continue;
        }

        std::snprintf(msg, sizeof msg, "%u pontos carregados de '%s'",
                      static_cast<unsigned>(carregador.pontos()), nome);
        log.info("base", msg);
        if (nome == coruja::kArquivoBak) {
            log.warning("base", "operando pela RESERVA: a base vigente nao "
                                "validou. Atualize quando puder");
        }
        return carregador.pontos();
    }

    // RF07: sem base o aparelho opera, e precisa deixar isso evidente.
    log.error("base", "NENHUMA base carregada: nem a vigente nem a reserva");
    return 0;
}

/// Lê e interpreta a configuração **do cartão, na hora do clique**.
///
/// Não no boot, e não embutida no firmware. O RNF03 é explícito: *"as
/// credenciais de Wi-Fi não podem estar embutidas no firmware; devem ser lidas
/// em tempo de execução de arquivo de configuração no cartão SD"*. Ler no
/// clique também significa que trocar o cartão passa a valer sem reiniciar o
/// aparelho — e que a configuração nunca fica velha em RAM.
bool carrega_configuracao(coruja::CartaoSd& cartao, coruja::Configuracao* destino,
                          coruja::Logger& log) {
    std::size_t      lidos = 0;
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

    std::snprintf(msg, sizeof msg, "config lida: %u rede(s), urls %s",
                  static_cast<unsigned>(lido.config.n_redes),
                  lido.config.tem_urls() ? "ok" : "AUSENTES");
    log.info("config", msg);
    if (!lido.diagnostico.limpo()) {
        std::snprintf(msg, sizeof msg,
                      "avisos: %u sem '=', %u desconhecidas, %u longas, "
                      "%u fora de faixa, %u incompletas",
                      static_cast<unsigned>(lido.diagnostico.linhas_sem_igual),
                      static_cast<unsigned>(lido.diagnostico.chaves_desconhecidas),
                      static_cast<unsigned>(lido.diagnostico.valores_longos),
                      static_cast<unsigned>(lido.diagnostico.indices_fora),
                      static_cast<unsigned>(lido.diagnostico.redes_incompletas));
        log.warning("config", msg);
    }
    return lido.config.ota_possivel();
}

}  // namespace

int main() {
    stdio_init_all();
    coruja::LoggerConsole console(nullptr, coruja::Nivel::Debug);
    coruja::CartaoSd     cartao;

    // O logger de cartão encadeia no console: nada deixa de aparecer na
    // serial por estar sendo gravado. Começa desligado — quem liga é a
    // configuração, alguns passos abaixo.
    coruja::LoggerCartao log(console, cartao, kArquivoLog);

    sleep_ms(2000);
    log.info("boot", "Coruja GPS — bancada de rede");

    char msg[110];
    std::snprintf(msg, sizeof msg, "base reservada: %u pontos x %u B = %u KiB",
                  static_cast<unsigned>(coruja::kCapacidadeFirmware),
                  static_cast<unsigned>(sizeof(coruja::Ponto)),
                  static_cast<unsigned>(sizeof g_pontos / 1024));
    log.info("mem", msg);


    coruja::LedRgbAnodoComum led;
    coruja::EncoderKy040     encoder;
    coruja::CicloCores       ciclo;
    coruja::RedeWifi         rede;
    coruja::AtualizadorOta   ota;

    cartao.inicia(log);

    // A configuração é lida AQUI, antes de tudo, só para aplicar as
    // preferências de log — e relida a cada clique, que é o que vale para o
    // Wi-Fi e as URLs. Sem esta leitura, `log_to_sd` só entraria em vigor no
    // primeiro clique e o boot inteiro ficaria de fora do arquivo. O boot é
    // justamente onde o diagnóstico rende mais.
    {
        coruja::Configuracao inicial;
        if (carrega_configuracao(cartao, &inicial, log)) {
            // ignorado: aqui só interessam as preferências de log
        }
        log.define_nivel_minimo(inicial.nivel_log);
        log.grava_no_cartao(inicial.log_para_cartao);
        std::snprintf(msg, sizeof msg, "nivel %s, gravacao no cartao %s",
                      coruja::nome_nivel(inicial.nivel_log),
                      inicial.log_para_cartao ? "LIGADA" : "desligada");
        log.info("log", msg);
    }
    const std::size_t pontos = carrega_base_do_cartao(cartao, log);
    std::snprintf(msg, sizeof msg, "base em memoria: %u de %u pontos possiveis",
                  static_cast<unsigned>(pontos),
                  static_cast<unsigned>(coruja::kCapacidadeFirmware));
    log.info("base", msg);

    log.info("ihm", "girar = cor do estado de via | clicar = atualizar base");
    log.info("ihm", "ordem: segura(verde) -> ambar -> rosa -> perigo(vermelho)");
    led.define_cor(ciclo.cor());
    std::snprintf(msg, sizeof msg, "estado inicial: %s",
                  coruja::nome_estado(ciclo.estado()));
    log.info("ihm", msg);

    while (true) {
        const auto evento = encoder.proximo_evento();

        if (evento == coruja::EventoEncoder::Clique) {
            // O LED vai a apagado durante a atualização: é a única pista que
            // quem está olhando o aparelho, e não o console, tem de que algo
            // está acontecendo. Ao fim, volta à cor onde o encoder estava.
            led.define_cor(coruja::cores::kApagado);

            // O OTA vai mexer no cartão por bastante tempo. Descarrega o que
            // está acumulado antes: se algo travar no meio, o que já
            // aconteceu estará gravado.
            log.descarrega();

            // A configuração é lida AQUI, a cada clique, e não guardada do
            // boot: trocar o cartão passa a valer sem reiniciar, e a leitura
            // nunca fica velha.
            coruja::Configuracao config;
            if (carrega_configuracao(cartao, &config, log)) {
                ota.executa(config, cartao, rede, log);
            } else {
                log.error("ota", "sem configuracao utilizavel: nada a fazer");
            }
            // As preferências de log podem ter mudado com o cartão.
            log.define_nivel_minimo(config.nivel_log);
            log.grava_no_cartao(config.log_para_cartao);
            log.descarrega();
            led.define_cor(ciclo.cor());
            std::snprintf(msg, sizeof msg, "de volta ao estado %s",
                          coruja::nome_estado(ciclo.estado()));
            log.info("ihm", msg);
        } else if (evento != coruja::EventoEncoder::Nenhum) {
            const auto estado = ciclo.aplica(evento);
            const auto cor = ciclo.cor();
            led.define_cor(cor);

            std::snprintf(msg, sizeof msg, "%-18s rgb=%3u,%3u,%3u%s",
                          coruja::nome_estado(estado), cor.r, cor.g, cor.b,
                          ciclo.na_ponta() ? "  (ponta da lista)" : "");
            log.info("ihm", msg);
        }

        sleep_ms(kPeriodoAmostragemMs);
    }
}
