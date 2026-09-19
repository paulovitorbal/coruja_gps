// Diagnóstico de bancada dos pinos do encoder.
//
// Existe porque o modo de calibração mostrou que o clique funciona e o giro
// não, e a partir daí só havia hipóteses. Este arquivo troca hipótese por
// medição.
//
// Compile com -DCORUJA_DIAGNOSTICO=ON.
//
// Três lições ficaram no desenho, cada uma de uma versão que falhou:
//
//  1. **Não imprimir durante a amostragem.** A v1 fazia `printf` a cada
//     mudança, e escrever em USB-CDC leva milissegundos — tempo em que o
//     encoder avança e estados se perdem. Pior: perder dois estados seguidos
//     produz uma transição que *parece* válida, então o defeito aparece como
//     dado limpo e errado.
//  2. **Não depender de sincronia com quem está na bancada.** A v2 media em
//     janelas anunciadas por mensagem na serial, e quem está com a placa na
//     mão pode não ter terminal ao vivo para ver o aviso. As janelas passaram
//     em branco e o veredito saiu "não é quadratura" — conclusão falsa tirada
//     de silêncio. Aqui a amostragem é **contínua**.
//  3. **Ceder tempo ao USB, e só com `sleep_ms`.** A v3 amostrava com
//     `busy_wait_us` num laço fechado e emudeceu. A v4 trocou por
//     `sleep_us(100)` e emudeceu igual: com a pool de alarmes padrão, esse
//     `sleep_us` vira `sleep_until`, ou seja dez mil alarmes por segundo, que
//     atropelam o alarme de que o `stdio_usb` depende. A v5 amostra em
//     **rajadas** de 1 ms com `busy_wait` e cede com `sleep_ms(1)`.
#include <hardware/gpio.h>
#include <pico/stdlib.h>
#include <pico/time.h>

#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "log/LoggerConsole.h"
#include "placa/Pinos.h"

namespace {

constexpr std::size_t   kCapacidade    = 600;
/// Passo dentro da rajada. 50 µs dá resolução de sobra para um estado de
/// centenas de microssegundos.
constexpr unsigned      kPassoRajadaUs = 50;
/// Quantas amostras por rajada antes de ceder o processador. 20 × 50 µs = 1 ms
/// de amostragem fina, seguidos de 1 ms de folga.
constexpr unsigned      kAmostrasPorRajada = 20;
constexpr std::uint32_t kIntervaloRelatorioUs = 2'500'000;
constexpr std::uint32_t kIntervaloBatimentoUs = 5'000'000;

struct Amostra {
    std::uint32_t us;
    std::uint8_t  estado;  ///< bit1 = CLK, bit0 = DT
};

Amostra g_buf[kCapacidade];

std::uint8_t le_estado() {
    return static_cast<std::uint8_t>(
        (gpio_get(coruja::pinos::kEncoderClk) ? 0x02 : 0x00) |
        (gpio_get(coruja::pinos::kEncoderDt) ? 0x01 : 0x00));
}

const char* nome(std::uint8_t e) {
    switch (e & 0x03) {
        case 0:  return "(0,0)";
        case 1:  return "(0,1)";
        case 2:  return "(1,0)";
        default: return "(1,1)";
    }
}

}  // namespace

int main() {
    stdio_init_all();
    coruja::LoggerConsole log(nullptr, coruja::Nivel::Debug);
    sleep_ms(2000);

    for (unsigned g : {coruja::pinos::kEncoderClk, coruja::pinos::kEncoderDt,
                       coruja::pinos::kEncoderSw}) {
        gpio_init(g);
        gpio_set_dir(g, GPIO_IN);
        gpio_pull_up(g);
    }

    char msg[110];
    std::snprintf(msg, sizeof msg,
                  "CLK=GPIO%u  DT=GPIO%u  SW=GPIO%u  |  rajadas de 20 x 50 us",
                  coruja::pinos::kEncoderClk, coruja::pinos::kEncoderDt,
                  coruja::pinos::kEncoderSw);
    log.info("diag", msg);
    log.info("diag", "gire quando quiser; o relatorio sai sozinho");

    std::uint8_t     anterior = le_estado();
    std::size_t      n = 0;
    absolute_time_t  t0 = get_absolute_time();
    absolute_time_t  ultimo_relatorio = t0;
    absolute_time_t  ultimo_batimento = t0;
    unsigned long    total = 0;

    while (true) {
        // Rajada: amostra fino sem nenhuma E/S nem chamada de temporizador.
        for (unsigned k = 0; k < kAmostrasPorRajada; ++k) {
            const std::uint8_t agora = le_estado();
            if (agora != anterior) {
                if (n < kCapacidade) {
                    g_buf[n] = {
                        static_cast<std::uint32_t>(
                            absolute_time_diff_us(t0, get_absolute_time())),
                        agora};
                    ++n;
                }
                ++total;
                anterior = agora;
            }
            busy_wait_us(kPassoRajadaUs);
        }

        const absolute_time_t agora_t = get_absolute_time();

        // Batimento: prova que o firmware está vivo mesmo sem ninguém girar.
        // Sem ele, "nenhum relatório" é ambíguo entre parado e travado — foi
        // o que atrapalhou o diagnóstico da v2.
        if (absolute_time_diff_us(ultimo_batimento, agora_t) >
            kIntervaloBatimentoUs) {
            ultimo_batimento = agora_t;
            if (n == 0) {
                std::snprintf(msg, sizeof msg,
                              "vivo. parado em %s | SW=%d | total de mudancas: %lu",
                              nome(anterior),
                              gpio_get(coruja::pinos::kEncoderSw) ? 1 : 0, total);
                log.info("diag", msg);
            }
        }

        if (n > 0 && absolute_time_diff_us(ultimo_relatorio, agora_t) >
                         kIntervaloRelatorioUs) {
            std::size_t vistos[4] = {0, 0, 0, 0};
            for (std::size_t i = 0; i < n; ++i) {
                ++vistos[g_buf[i].estado & 0x03];
            }

            std::snprintf(msg, sizeof msg, "=== %u mudancas ===",
                          static_cast<unsigned>(n));
            log.info("diag", msg);

            const std::size_t mostrar = n < 20 ? n : 20;
            for (std::size_t i = 0; i < mostrar; ++i) {
                const std::uint32_t dur =
                    (i + 1 < n ? g_buf[i + 1].us - g_buf[i].us : 0);
                std::snprintf(msg, sizeof msg, "%3u  %s  durou %6u us",
                              static_cast<unsigned>(i), nome(g_buf[i].estado),
                              dur);
                log.info("diag", msg);
            }
            std::snprintf(msg, sizeof msg,
                          "ocorrencias: (0,0)=%u (0,1)=%u (1,0)=%u (1,1)=%u",
                          static_cast<unsigned>(vistos[0]),
                          static_cast<unsigned>(vistos[1]),
                          static_cast<unsigned>(vistos[2]),
                          static_cast<unsigned>(vistos[3]));
            log.info("diag", msg);
            if (vistos[0] == 0) {
                log.warning("diag",
                            "(0,0) NUNCA ocorreu -> os canais nao se sobrepoem");
            } else {
                log.info("diag", "(0,0) ocorre -> e quadratura padrao");
            }

            n = 0;
            t0 = get_absolute_time();
            anterior = le_estado();
            ultimo_relatorio = get_absolute_time();
            ultimo_batimento = ultimo_relatorio;
        }

        // O yield tem de ser `sleep_ms(1)`, e nada menor.
        //
        // `busy_wait_us` não cede nada e a placa emudeceu (v3). `sleep_us(100)`
        // parece ceder, mas com a pool de alarmes padrão ele vira
        // `sleep_until` — dez mil alarmes por segundo, que atropelam o alarme
        // que o `stdio_usb` usa para chamar `tud_task()`, e a placa emudeceu
        // igual (v4). Tudo que usou `sleep_ms(1)` falou; nada mais falou.
        sleep_ms(1);
    }
}
