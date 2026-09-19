// Ponto de entrada. Hoje roda o MODO DE CALIBRAÇÃO, que fecha a metade
// pendente do R-05: as razões de PWM do âmbar e do rosa.
//
//   girar   -> ajusta o canal variável do item atual
//   clicar  -> avança: vermelho, verde, azul, âmbar, rosa, e volta
//
// Ao dar a volta, imprime o bloco pronto para colar no gera_config.py.
//
// De passagem valida a fiação inteira do encoder e do LED, inclusive o canal
// verde, que o modo de teste anterior nunca acendia. Para voltar àquele modo
// — esquerda vermelho, direita azul, clique apaga — troque ModoCalibracao por
// ModoTesteEncoder, que continua no projeto e testado.
//
// Não é o comportamento de produção: em operação o encoder ajusta o brilho e
// comanda a atualização OTA, e o LED carrega o estado de via
// (`requirements.md` matriz de IHM). Ver a tabela de estado no README.
#include <pico/stdlib.h>

#include <cstdio>

#include "app/ModoCalibracao.h"
#include "encoder/EncoderKy040.h"
#include "led/LedRgbAnodoComum.h"
#include "log/LoggerConsole.h"
#include "nucleo/BaseRadares.h"
#include "nucleo/LimiarInfracao.h"

namespace {

/// A base vive em `.bss`, dimensionada pelo teto do formato. Não há alocação
/// dinâmica em nenhum ponto do caminho crítico.
coruja::Ponto g_pontos[coruja::kCapacidadeFirmware];

/// Período de amostragem do encoder.
///
/// **Não pode ser o 4 Hz do laço de navegação.** A decodificação por tabela
/// precisa ver cada uma das quatro transições de um detente; a 4 Hz um giro
/// rápido pularia estados e os passos seriam perdidos ou lidos ao contrário.
/// Com 1 ms há folga de sobra para os ~20 detentes por volta do KY-040, mesmo
/// girando depressa.
constexpr std::uint32_t kPeriodoAmostragemMs = 1;

/// Imprime o bloco pronto para transcrever em `scripts/gera_config.py`.
///
/// Sai a cada volta completa do ciclo, que é o momento em que todos os itens
/// já foram vistos ao menos uma vez.
void imprime_resumo(coruja::Logger& log, const coruja::ModoCalibracao& modo) {
    using coruja::ItemCalibracao;
    char linha[96];

    log.info("calib", "---- calibracao do R-05, para o gera_config.py ----");
    std::snprintf(linha, sizeof linha, "  duty do verde no ambar : %.2f",
                  static_cast<double>(modo.razao(ItemCalibracao::Ambar)));
    log.info("calib", linha);
    std::snprintf(linha, sizeof linha, "  duty do azul no rosa   : %.2f",
                  static_cast<double>(modo.razao(ItemCalibracao::Rosa)));
    log.info("calib", linha);
    log.info("calib", "  (o vermelho das duas fica em 1,00 por construcao)");
    std::snprintf(linha, sizeof linha,
                  "  canais isolados: R=%.2f  G=%.2f  B=%.2f",
                  static_cast<double>(modo.razao(ItemCalibracao::Vermelho)),
                  static_cast<double>(modo.razao(ItemCalibracao::Verde)),
                  static_cast<double>(modo.razao(ItemCalibracao::Azul)));
    log.info("calib", linha);
    log.info("calib", "--------------------------------------------------");
}

}  // namespace

int main() {
    stdio_init_all();
    coruja::LoggerConsole log(nullptr, coruja::Nivel::Debug);

    // Dar tempo ao host de abrir o terminal antes do primeiro log.
    sleep_ms(2000);
    log.info("boot", "Coruja GPS — modo de teste de bancada");

    char msg[96];
    std::snprintf(msg, sizeof msg, "base reservada: %u pontos x %u B = %u KiB",
                  static_cast<unsigned>(coruja::kCapacidadeFirmware),
                  static_cast<unsigned>(sizeof(coruja::Ponto)),
                  static_cast<unsigned>(sizeof g_pontos / 1024));
    log.info("mem", msg);

    // Exercita o caminho real de carga. Sem cartao nao ha bytes, e a base
    // recusa — que e exatamente o estado de Falha de Dados do RF07. Isto
    // tambem **referencia** g_pontos: se so `sizeof` fosse usado, o linker
    // eliminaria o array e a reserva de memoria nao existiria de fato.
    const auto carga = coruja::carrega_base(nullptr, 0, g_pontos,
                                            coruja::kCapacidadeFirmware, &log);
    std::snprintf(msg, sizeof msg, "carga sem cartao: %s", descreve(carga.erro));
    log.info("base", msg);
    std::snprintf(msg, sizeof msg, "V_infra(80) = %.1f  V_infra(110) = %.1f",
                  static_cast<double>(coruja::velocidade_infracao(80)),
                  static_cast<double>(coruja::velocidade_infracao(110)));
    log.info("limiar", msg);
    log.warning("boot", "sem base carregada: leitor SD ainda nao implementado");

    coruja::LedRgbAnodoComum led;
    coruja::EncoderKy040     encoder;
    coruja::ModoCalibracao   modo;

    log.info("calib", "gire = ajusta | clique = proximo item");
    log.info("calib", "ordem: vermelho, verde, azul, ambar(R+G), rosa(R+B)");
    log.warning("calib", "julgue o ROSA ao lado do VERMELHO, e sob sol direto");
    led.define_cor(modo.cor());

    while (true) {
        const auto evento = encoder.proximo_evento();
        if (evento != coruja::EventoEncoder::Nenhum) {
            const auto cor = modo.aplica(evento);
            led.define_cor(cor);

            std::snprintf(msg, sizeof msg, "%-14s duty=%3u (%.2f)  rgb=%u,%u,%u",
                          coruja::nome_item(modo.item()),
                          modo.duty(modo.item()),
                          static_cast<double>(modo.razao(modo.item())),
                          cor.r, cor.g, cor.b);
            log.info("calib", msg);

            if (modo.completou_ciclo()) {
                imprime_resumo(log, modo);
            }
        }
        sleep_ms(kPeriodoAmostragemMs);
    }
}
