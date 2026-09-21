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

#include "app/CicloCores.h"
#include "encoder/EncoderKy040.h"
#include "led/LedRgbAnodoComum.h"
#include "log/LoggerConsole.h"
#include "armazenamento/CartaoSd.h"
#include "nucleo/BaseRadares.h"
#include "placa/Pinos.h"
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
char g_texto_config[kTamBufferConfig];

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
    coruja::LoggerConsole log(nullptr, coruja::Nivel::Debug);

    sleep_ms(2000);
    log.info("boot", "Coruja GPS — bancada de rede");

    char msg[110];
    std::snprintf(msg, sizeof msg, "base reservada: %u pontos x %u B = %u KiB",
                  static_cast<unsigned>(coruja::kCapacidadeFirmware),
                  static_cast<unsigned>(sizeof(coruja::Ponto)),
                  static_cast<unsigned>(sizeof g_pontos / 1024));
    log.info("mem", msg);

    // Referencia g_pontos de verdade: se só `sizeof` fosse usado, o linker
    // eliminaria o array e a reserva de memória não existiria de fato.
    const auto carga = coruja::carrega_base(nullptr, 0, g_pontos,
                                            coruja::kCapacidadeFirmware, &log);
    std::snprintf(msg, sizeof msg, "carga sem cartao: %s", descreve(carga.erro));
    log.info("base", msg);

    coruja::LedRgbAnodoComum led;
    coruja::EncoderKy040     encoder;
    coruja::CicloCores       ciclo;
    coruja::CartaoSd         cartao;
    coruja::RedeWifi         rede;
    coruja::AtualizadorOta   ota;

    // No boot o cartão é só INSPECIONADO, não lido. O estado dele é a primeira
    // coisa que quem está na bancada precisa saber, e era o que faltava: sem
    // esta linha, cartão ausente e cartão presente davam exatamente o mesmo
    // console, e a diferença só aparecia depois, disfarçada de outro erro.
    // Antes de o driver configurar o pino: ele impõe o pull dele.
    cartao.diagnostica_det(log);
    cartao.inicia(log);
    // O nível vem MEDIDO, não deduzido do significado: um texto que afirmava
    // "(DET em nivel baixo)" já saiu com o pino em alto, depois que a
    // polaridade foi corrigida. Ver R-41.
    std::snprintf(msg, sizeof msg, "DET (GPIO %u) em nivel %s",
                  coruja::pinos::kSdDet, cartao.nivel_bruto() ? "ALTO" : "BAIXO");
    log.info("sd", msg);
    if (cartao.presente()) {
        log.info("sd", "cartao presente no slot");
    } else {
        log.warning("sd", "SEM CARTAO no slot");
        log.warning("sd", "o clique nao vai atualizar: o coruja.cfg vive no cartao");
    }

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

            // A configuração é lida AQUI, a cada clique, e não guardada do
            // boot: trocar o cartão passa a valer sem reiniciar, e a leitura
            // nunca fica velha.
            // O diagnóstico do DET roda a CADA clique, não só no boot: é o
            // que permite mover o fio de pino em pino e conferir sem
            // reiniciar nem regravar.
            cartao.diagnostica_det(log);

            coruja::Configuracao config;
            if (carrega_configuracao(cartao, &config, log)) {
                ota.executa(config, rede, log);
            } else {
                log.error("ota", "sem configuracao utilizavel: nada a fazer");
            }
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
