// Ponto de entrada. Hoje roda o MODO DE TESTE DE BANCADA: valida a fiação e a
// decodificação do encoder contra o LED RGB, sem depender de GPS, cartão nem
// display, que ainda não estão implementados.
//
//   girar à esquerda  -> vermelho
//   girar à direita   -> azul
//   clicar            -> apaga
//
// Não é o comportamento de produção: em operação o encoder ajusta o brilho e
// comanda a atualização OTA, e o LED carrega o estado de via
// (`requirements.md` matriz de IHM). Ver a tabela de estado no README.
#include <pico/stdlib.h>

#include <cstdio>

#include "app/ModoTesteEncoder.h"
#include "encoder/EncoderKy040.h"
#include "led/LedRgbPwm.h"
#include "log/LoggerConsole.h"
#include "nucleo/BaseRadares.h"
#include "nucleo/LimiarInfracao.h"

namespace {

/// A base vive em `.bss`, dimensionada pelo teto do formato. Não há alocação
/// dinâmica em nenhum ponto do caminho crítico.
coruja::Ponto g_pontos[coruja::kTetoPontos];

/// Período de amostragem do encoder.
///
/// **Não pode ser o 4 Hz do laço de navegação.** A decodificação por tabela
/// precisa ver cada uma das quatro transições de um detente; a 4 Hz um giro
/// rápido pularia estados e os passos seriam perdidos ou lidos ao contrário.
/// Com 1 ms há folga de sobra para os ~20 detentes por volta do KY-040, mesmo
/// girando depressa.
constexpr std::uint32_t kPeriodoAmostragemMs = 1;

}  // namespace

int main() {
    stdio_init_all();
    coruja::LoggerConsole log(nullptr, coruja::Nivel::Debug);

    // Dar tempo ao host de abrir o terminal antes do primeiro log.
    sleep_ms(2000);
    log.info("boot", "Coruja GPS — modo de teste de bancada");

    char msg[96];
    std::snprintf(msg, sizeof msg, "base reservada: %u pontos, %u B em .bss",
                  static_cast<unsigned>(coruja::kTetoPontos),
                  static_cast<unsigned>(sizeof g_pontos));
    log.info("mem", msg);
    std::snprintf(msg, sizeof msg, "V_infra(80) = %.1f  V_infra(110) = %.1f",
                  static_cast<double>(coruja::velocidade_infracao(80)),
                  static_cast<double>(coruja::velocidade_infracao(110)));
    log.info("limiar", msg);
    log.warning("boot", "sem base carregada: leitor SD ainda nao implementado");

    coruja::LedRgbPwm      led;
    coruja::EncoderKy040   encoder;
    coruja::ModoTesteEncoder modo;

    log.info("teste", "gire p/ esquerda = vermelho, p/ direita = azul, "
                      "clique = apaga");

    while (true) {
        const auto evento = encoder.proximo_evento();
        if (evento != coruja::EventoEncoder::Nenhum) {
            const auto cor = modo.aplica(evento);
            led.define_cor(cor);
            std::snprintf(msg, sizeof msg, "%s -> r=%u g=%u b=%u",
                          coruja::nome_evento(evento), cor.r, cor.g, cor.b);
            log.debug("teste", msg);
        }
        sleep_ms(kPeriodoAmostragemMs);
    }
}
