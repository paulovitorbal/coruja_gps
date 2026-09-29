// Primeira ligacao do display (R-62).
//
// **Existe porque o modulo nao tem MISO.** Nada pode ser lido de volta do
// ST7789V -- nem o ID, nem se um comando foi aceito. A unica verificacao
// possivel e olhar a tela ou medir os pinos, e um programa que so desenha
// a tela final nao diz *onde* falhou quando ela fica escura.
//
// Escada de diagnostico, da causa mais barata de descartar para a mais
// cara. Cada etapa so faz sentido se a anterior passou:
//
//   1. rampa do backlight -- so PWM. Nao acende? E VCC, GND ou BL.
//   2. teste de continuidade -- cada pino de controle pulsa sozinho,
//      anunciado, por 6 s. E para o multimetro: prova a FIACAO antes de
//      acusar o protocolo. Um DC ou CS trocado da exatamente o mesmo
//      sintoma que um clock rapido demais.
//   3. varredura de velocidade -- o mesmo init em varias frequencias, cada
//      uma com sua cor. Diz em qual delas o painel passa a receber, em vez
//      de deixar a escolha no chute.
//   4. cores nomeadas, com e sem INVON.
//   5. orientacao, com padrao assimetrico de proposito.
//   6. paleta do RF03 no piso de 5% (R-05 no painel real).

#include <cstdio>

#include <pico/stdio_usb.h>
#include <pico/stdlib.h>

#include "display/PainelSt7789.h"
#include "led/LedRgbAnodoComum.h"
#include "display/RetroiluminacaoPwm.h"
#include "display/Visor.h"
#include "placa/Pinos.h"

using namespace coruja;

namespace {

constexpr std::uint32_t kEtapaMs = 3000;

/// O LED e o unico retorno que sobra enquanto a tela esta muda.
///
/// Uma cor por etapa, lida de relance, mais uma piscada rapida ao entrar
/// nela. Serve para dois fins: saber que o programa progride em vez de
/// ter travado, e -- se ele parar numa cor -- saber exatamente em qual
/// etapa parou, sem precisar do terminal.
LedRgbAnodoComum* g_led = nullptr;

void pisca_etapa(const Cor& cor, int quantas) {
    if (g_led == nullptr) { return; }
    for (int i = 0; i < quantas; ++i) {
        g_led->define_cor(cor);
        sleep_ms(120);
        g_led->define_cor(cores::kApagado);
        sleep_ms(120);
    }
    g_led->define_cor(cor);  // fica aceso durante a etapa inteira
}

void anuncia(const char* texto) { std::printf("\n=== %s\n", texto); }

void mostra(PainelSt7789& painel, const char* nome, std::uint16_t cor) {
    std::printf("  %s\n", nome);
    painel.limpa(cor);
    sleep_ms(kEtapaMs);
}

void rampa(RetroiluminacaoPwm& luz) {
    pisca_etapa(cores::kVermelho, 1);
    anuncia("1/6 rampa do backlight (sem SPI)");
    std::printf("  se nao acender, o problema e VCC, GND ou BL\n");
    for (int pct = 0; pct <= 100; pct += 5) {
        luz.define_duty(static_cast<std::uint16_t>(pct * 655));
        sleep_ms(60);
    }
    for (int pct = 100; pct >= 0; pct -= 5) {
        luz.define_duty(static_cast<std::uint16_t>(pct * 655));
        sleep_ms(60);
    }
    luz.define_duty(65535);
}

/// Etapa 2: prova a fiacao, nao o protocolo.
///
/// Pulsa um pino de cada vez a 1 Hz, por 6 s, anunciando qual. Com a ponta
/// do multimetro no pino do MODULO -- nao no do Pico -- da para ver o
/// valor oscilar. Se um deles nao oscila, o fio esta solto ou trocado, e
/// nenhuma quantidade de ajuste de protocolo vai consertar isso.
void continuidade() {
    pisca_etapa(cores::kAmarelo, 2);
    anuncia("2/6 continuidade dos pinos de controle");
    std::printf("  ponta do multimetro no pino DO MODULO, 6 s cada\n");
    const struct { const char* nome; unsigned pino; } linhas[] = {
        {"CS  (deve oscilar no pino CS do modulo)",  pinos::kDisplayCs},
        {"DC  (deve oscilar no pino DC do modulo)",  pinos::kDisplayDc},
        {"RST (deve oscilar no pino RST do modulo)", pinos::kDisplayRst},
        {"SCL (deve oscilar no pino SCL do modulo)", pinos::kDisplaySck},
        {"SDA (deve oscilar no pino SDA do modulo)", pinos::kDisplayMosi},
    };
    for (const auto& l : linhas) {
        std::printf("  %s\n", l.nome);
        // SCL e SDA estao em funcao SPI; volta para GPIO so aqui.
        gpio_set_function(l.pino, GPIO_FUNC_SIO);
        gpio_init(l.pino);
        gpio_set_dir(l.pino, GPIO_OUT);
        for (int i = 0; i < 6; ++i) {
            gpio_put(l.pino, true);
            sleep_ms(500);
            gpio_put(l.pino, false);
            sleep_ms(500);
        }
    }
    std::printf("  fim da continuidade\n");
}

/// Etapa 3: a mesma inicializacao em varias velocidades, uma cor por
/// velocidade. O painel aceita ~66 MHz no papel; o que decide e o fio.
bool varredura(PainelSt7789& painel) {
    pisca_etapa(cores::kVerde, 3);
    anuncia("3/6 varredura de velocidade do SPI");
    std::printf("  uma cor por velocidade; diga em quais apareceu algo\n");
    const struct { std::uint32_t hz; const char* cor; std::uint16_t v; }
    passos[] = {
        {  500000, "AZUL      (0,5 MHz)", 0x001F},
        { 1000000, "VERDE     (1 MHz)",   0x07E0},
        { 4000000, "VERMELHO  (4 MHz)",   0xF800},
        {16000000, "AMARELO   (16 MHz)",  0xFFE0},
        {32000000, "BRANCO    (32 MHz)",  0xFFFF},
    };
    for (const auto& p : passos) {
        painel.inicia(p.hz);
        std::printf("  %s -- efetivo %lu Hz\n", p.cor,
                    static_cast<unsigned long>(painel.baud_efetivo()));
        painel.limpa(p.v);
        sleep_ms(kEtapaMs);
    }
    // Modo 3 (CPOL=1, CPHA=1) a 1 MHz. O ST7789V amostra na borda de
    // subida e os dois modos entregam isso, mas as bibliotecas se dividem
    // -- uma cor propria elimina a duvida em vez de deixa-la aberta.
    std::printf("  MAGENTA   (1 MHz, modo 3 em vez do modo 0)\n");
    painel.inicia(1000000, /*modo3=*/true);
    painel.limpa(0xF81F);
    sleep_ms(kEtapaMs);

    // Segue na mais conservadora que faz sentido manter.
    painel.inicia(1000000);
    return true;
}

void serie_rgb(PainelSt7789& painel) {
    mostra(painel, "vermelho", 0xF800);
    mostra(painel, "verde", 0x07E0);
    mostra(painel, "azul", 0x001F);
}

void orientacao(PainelSt7789& painel) {
    pisca_etapa(cores::kRosa, 5);
    anuncia("5/6 orientacao");
    std::printf("  esperado: barra branca no TOPO, quadrado vermelho no\n"
                "  canto SUPERIOR ESQUERDO, faixa azul fina embaixo\n");
    painel.limpa(0x0000);
    painel.preenche(0, 0, tela::kLargura, 12, 0xFFFF);
    painel.preenche(0, 0, 40, 40, 0xF800);
    painel.preenche(0, tela::kAltura - 6, tela::kLargura, 6, 0x001F);
    sleep_ms(kEtapaMs * 2);
}

void paleta_rf03(PainelSt7789& painel, RetroiluminacaoPwm& luz) {
    pisca_etapa(Cor{255, 255, 255}, 6);
    anuncia("6/6 paleta do RF03 no piso de 5%");
    const std::uint16_t cores[] = {paleta::kBarraAmbar, paleta::kBarraRosa,
                                   paleta::kBarraPerigo};
    const int largura = tela::kLargura / 3;
    painel.limpa(paleta::kFundo);
    for (int i = 0; i < 3; ++i) {
        painel.preenche(i * largura, 40, largura, tela::kAltura - 80,
                        cores[i]);
    }
    std::printf("  ambar | rosa | perigo -- a 100%% de brilho\n");
    luz.define_duty(65535);
    sleep_ms(kEtapaMs);
    std::printf("  as mesmas tres a 5%% (R-05: da para distinguir?)\n");
    luz.define_duty(90);
    sleep_ms(kEtapaMs * 2);
    luz.define_duty(65535);
}

}  // namespace

int main() {
    stdio_init_all();
    // Espera o monitor, ate 30 s, e segue sem ninguem: a ordem correta de
    // energizar e 12 V primeiro, USB depois, entao o Pico ja esta rodando
    // quando o terminal chega.
    for (int i = 0; i < 300 && !stdio_usb_connected(); ++i) {
        sleep_ms(100);
    }
    sleep_ms(300);

    RetroiluminacaoPwm luz;
    PainelSt7789 painel;
    LedRgbAnodoComum led;
    g_led = &led;

    std::printf("\ncoruja - bancada do display ST7789V (GMT024-08-SPI8P)\n");

    rampa(luz);
    continuidade();
    varredura(painel);

    pisca_etapa(cores::kAzul, 4);
    anuncia("4/6 cores com INVON (o padrao para IPS)");
    std::printf("  se o 'vermelho' aparecer ciano, a inversao esta errada\n");
    serie_rgb(painel);
    std::printf("  as mesmas com INVOFF:\n");
    painel.define_inversao(false);
    serie_rgb(painel);
    painel.define_inversao(true);

    orientacao(painel);
    paleta_rf03(painel, luz);

    std::printf("\nfim. repetindo as cores em laco.\n");
    // No laco o LED acompanha a cor que a tela deveria estar mostrando:
    // se a tela ficar muda, da para conferir que o firmware nao travou.
    while (true) {
        led.define_cor(cores::kVermelho);
        mostra(painel, "vermelho", 0xF800);
        led.define_cor(cores::kVerde);
        mostra(painel, "verde", 0x07E0);
        led.define_cor(cores::kAzul);
        mostra(painel, "azul", 0x001F);
    }
}
