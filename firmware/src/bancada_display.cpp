// Bancada do display: as TELAS DE VERDADE no painel real.
//
// Não é mais maquete. O que roda aqui é a `TelaPrincipal` e o `TelaMenu` do
// produto, através do `VisorSt7789` — as mesmas classes que a suíte de host
// exercita contra um dublê. O que esta bancada acrescenta é a única coisa
// que o host não dá: **olhar**.
//
// Não há GPS nem cartão. A telemetria é roteirizada, porque o objetivo é
// julgar tela e não integração — e porque o GPS ainda não foi montado.
//
//   girar   ajusta o brilho (e a faixa superior mostra "BRILHO n%",
//           exatamente como no produto), ou navega o menu quando ele
//           estiver aberto
//   clicar  avança o roteiro; no cenário PARADO, abre o menu
//
// As cinco etapas de diagnóstico da primeira ligação continuam aqui, atrás
// de `kDiagnosticoCompleto`. Foram elas que isolaram alimentação, fiação,
// velocidade e inversão, e serão elas de novo quando a segunda unidade for
// montada — que é uma primeira ligação inteira outra vez.

#include <cstdio>

#include <pico/stdio_usb.h>
#include <pico/stdlib.h>

#include "display/Brilho.h"
#include "display/PainelSt7789.h"
#include "display/RetroiluminacaoPwm.h"
#include "display/TelaMenu.h"
#include "display/TelaPrincipal.h"
#include "display/VisorSt7789.h"
#include "encoder/EncoderKy040.h"
#include "led/LedRgbAnodoComum.h"
#include "menu/MenuAjustes.h"
#include "placa/Pinos.h"

using namespace coruja;

namespace {

/// As cinco etapas de diagnóstico ficam desligadas por padrão: levam ~1 min,
/// e 30 s disso é pulso de pinos para multímetro. Era essencial enquanto o
/// painel não desenhava; com ele validado, é espera pura. Trocar para `true`
/// religa — ver R-63.
constexpr bool kDiagnosticoCompleto = false;

constexpr std::uint32_t kEtapaMs = 3000;

LedRgbAnodoComum* g_led = nullptr;

void pisca_etapa(const Cor& cor, int quantas) {
    if (g_led == nullptr) { return; }
    for (int i = 0; i < quantas; ++i) {
        g_led->define_cor(cor);
        sleep_ms(120);
        g_led->define_cor(cores::kApagado);
        sleep_ms(120);
    }
    g_led->define_cor(cor);
}

void anuncia(const char* texto) { std::printf("\n=== %s\n", texto); }

void mostra(PainelSt7789& painel, const char* nome, std::uint16_t cor) {
    std::printf("  %s\n", nome);
    painel.limpa(cor);
    sleep_ms(kEtapaMs);
}

void rampa(RetroiluminacaoPwm& luz) {
    anuncia("1/5 rampa do backlight (sem SPI)");
    std::printf("  se nao acender, o problema e VCC, GND ou BL\n");
    for (int pct = 0; pct <= 100; pct += 5) {
        luz.define_duty(static_cast<std::uint16_t>(pct * 655));
        sleep_ms(60);
    }
    luz.define_duty(65535);
}

/// Pulsa um pino de cada vez a 1 Hz, para medição com multímetro no pino
/// **do módulo**. Prova a fiação antes de acusar o protocolo: um `DC` ou
/// `CS` trocado dá exatamente o mesmo sintoma que um clock rápido demais.
void continuidade() {
    anuncia("2/5 continuidade dos pinos de controle");
    const struct { const char* nome; unsigned pino; } linhas[] = {
        {"CS",  pinos::kDisplayCs},  {"DC",  pinos::kDisplayDc},
        {"RST", pinos::kDisplayRst}, {"SCL", pinos::kDisplaySck},
        {"SDA", pinos::kDisplayMosi},
    };
    for (const auto& l : linhas) {
        std::printf("  %s (no pino do modulo)\n", l.nome);
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
}

void varredura(PainelSt7789& painel) {
    anuncia("3/5 varredura de velocidade do SPI");
    const struct { std::uint32_t hz; const char* cor; std::uint16_t v; }
    passos[] = {
        {  500000, "AZUL      (0,5 MHz)", 0x001F},
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
    painel.inicia();
}

void orientacao(PainelSt7789& painel) {
    anuncia("5/5 orientacao");
    std::printf("  barra branca no TOPO, quadrado vermelho no canto\n"
                "  SUPERIOR ESQUERDO, faixa azul fina embaixo\n");
    painel.limpa(0x0000);
    painel.preenche(0, 0, tela::kLargura, 12, 0xFFFF);
    painel.preenche(0, 0, 40, 40, 0xF800);
    painel.preenche(0, tela::kAltura - 6, tela::kLargura, 6, 0x001F);
    sleep_ms(kEtapaMs * 2);
}

// ------------------------------------------------------ roteiro de telas

Ponto ponto(std::uint8_t limite, TipoPonto tipo) {
    Ponto p{};
    p.limite = limite;
    p.tipo = tipo;
    p.sentido = Sentido::Omnidirecional;
    return p;
}

/// Um instante da direção, montado à mão.
///
/// As velocidades contam uma história coerente com o RF03 — limite de 60,
/// `V_infra` de 66 — e os dois últimos cenários existem para julgar o
/// layout nos extremos: `120/120` é o pior caso de largura (R-64) e o
/// PARADO é onde o menu abre.
struct Cena {
    const char* nome;
    float       velocidade;
    bool        tem_alvo;
    std::uint8_t limite;
    TipoPonto   tipo;
    Zona        zona;
    float       distancia_m;
    bool        tem_fix;
    bool        base_ok;
};

constexpr Cena kRoteiro[] = {
    {"segura, sem ponto",   58, false,  0, TipoPonto::RadarFixo,
     Zona::Segura, 0, true, true},
    {"aproximacao 62/60",   62, true,  60, TipoPonto::RadarFixo,
     Zona::AproximacaoConforme, 240, true, true},
    {"margem 67/60",        67, true,  60, TipoPonto::RadarFixo,
     Zona::AproximacaoMargem, 150, true, true},
    {"perigo 74/60",        74, true,  60, TipoPonto::RadarFixo,
     Zona::Perigo, 60, true, true},
    {"semaforo, sem limite", 48, true,  0, TipoPonto::SemaforoCamera,
     Zona::Semaforo, 180, true, true},
    {"semaforo COM radar",  63, true,  60, TipoPonto::SemaforoComRadar,
     Zona::AproximacaoMargem, 120, true, true},
    {"rodovia 120/120",    120, true, 120, TipoPonto::RadarFixo,
     Zona::Perigo, 90, true, true},
    {"sem sinal",           0, false,  0, TipoPonto::RadarFixo,
     Zona::SemSinal, 0, false, true},
    {"base indisponivel",  55, false,  0, TipoPonto::RadarFixo,
     Zona::SemSinal, 0, true, false},
    {"PARADO -- gire para abrir o menu", 0, false, 0, TipoPonto::RadarFixo,
     Zona::Segura, 0, true, true},
};
constexpr int kQuantasCenas = static_cast<int>(sizeof kRoteiro /
                                               sizeof *kRoteiro);

EstadoTela monta(const Cena& c, const Brilho& brilho, std::uint32_t agora,
                 std::uint32_t brilho_em, bool houve_brilho) {
    EstadoTela e;
    e.telemetria.velocidade_kmh = c.velocidade;
    e.telemetria.data_valida = true;
    e.telemetria.ano = 2026;
    e.telemetria.mes = 9;
    e.telemetria.dia = 30;
    e.telemetria.hora = 12;
    e.telemetria.minuto = 34;
    e.tem_fix = c.tem_fix;
    e.base_disponivel = c.base_ok;
    e.veredito.zona = c.zona;
    e.veredito.tem_alvo = c.tem_alvo;
    e.veredito.distancia_m = c.distancia_m;
    if (c.tem_alvo) {
        e.veredito.alvo = ponto(c.limite, c.tipo);
    }
    e.sem_sinal_desde_ms = agora > 14000 ? agora - 14000 : 0;
    e.brilho_pct = brilho.percentual();
    e.brilho_mexido_em_ms = brilho_em;
    e.houve_ajuste_brilho = houve_brilho;
    return e;
}

/// O laço: as telas do produto, comandadas pelo encoder.
void telas_do_produto(PainelSt7789& painel, RetroiluminacaoPwm& luz,
                      Encoder& encoder, Brilho& brilho) {
    VisorSt7789 visor{painel};
    TelaPrincipal tela;
    TelaMenu tela_menu;
    MenuAjustes menu{Configuracao{}};

    // Numeros de mentira, mas plausiveis: a tela de informacao tem de ser
    // julgada com conteudo do tamanho do real. "0 pts" nao diria nada sobre
    // o layout.
    InfoAparelho info;
    std::snprintf(info.nome, sizeof info.nome, "%s", "fusca");
    std::snprintf(info.versao_base, sizeof info.versao_base, "%s",
                  "2026-09-15");
    info.pontos = 18304;
    info.taxa_hz = 4.0F;

    int cena = 0;
    bool menu_no_ar = false;
    std::uint32_t brilho_em = 0;
    bool houve_brilho = false;

    anuncia("telas do produto (TelaPrincipal e TelaMenu de verdade)");
    std::printf("  girar  = brilho, ou navega o menu quando aberto\n");
    std::printf("  clique = proxima cena (%d), ou age no menu\n",
                kQuantasCenas);
    std::printf("  cena: %s\n", kRoteiro[cena].nome);
    luz.define_duty(brilho.duty());

    std::uint32_t agora = 0;
    while (true) {
        agora = to_ms_since_boot(get_absolute_time());
        const EventoEncoder e = encoder.proximo_evento();

        // O menu só abre parado, como no produto (RF05.1). Aqui "parado" é
        // a cena, não o GPS — mas a regra é a mesma, e é ela que se quer ver.
        const bool parado = kRoteiro[cena].velocidade < 3.0F;
        if (menu.aberto() || (parado && e != EventoEncoder::Nenhum)) {
            const AcaoMenu acao = menu.avalia(e, parado, agora);
            if (acao == AcaoMenu::Gravar) {
                std::printf("  [gravaria os ajustes no cartao]\n");
            } else if (acao != AcaoMenu::Nenhuma) {
                std::printf("  [acao do menu: %d]\n", static_cast<int>(acao));
            }
            brilho.define_presets(menu.ajustes().brilho_dia,
                                  menu.ajustes().brilho_noite);
            luz.define_duty(brilho.duty());
        } else if (e == EventoEncoder::Clique) {
            cena = (cena + 1) % kQuantasCenas;
            std::printf("  cena: %s\n", kRoteiro[cena].nome);
        } else if (e == EventoEncoder::GiroDireita) {
            // Fora do menu o giro ajusta o brilho, como no produto com o
            // carro andando -- e a faixa superior mostra "BRILHO n%".
            brilho.aumenta();
            brilho_em = agora;
            houve_brilho = true;
            luz.define_duty(brilho.duty());
            std::printf("  brilho %u%%\n", brilho.percentual());
        } else if (e == EventoEncoder::GiroEsquerda) {
            brilho.diminui();
            brilho_em = agora;
            houve_brilho = true;
            luz.define_duty(brilho.duty());
            std::printf("  brilho %u%%\n", brilho.percentual());
        }

        // A transição entre telas invalida a que entra: as duas só
        // redesenham o que mudou, e o que a outra deixou no painel não está
        // em nenhum dos dois instantâneos.
        if (menu.aberto() != menu_no_ar) {
            if (menu.aberto()) { tela_menu.invalida(); } else { tela.invalida(); }
            menu_no_ar = menu.aberto();
        }

        if (menu.aberto()) {
            info.taxa_hz = 4.0F;
            tela_menu.desenha(menu, info, visor);
        } else {
            tela.desenha(monta(kRoteiro[cena], brilho, agora, brilho_em,
                               houve_brilho),
                         agora, visor);
        }
        sleep_ms(1);  // a quadratura precisa ver cada transicao
    }
}

}  // namespace

int main() {
    stdio_init_all();
    for (int i = 0; i < 300 && !stdio_usb_connected(); ++i) {
        sleep_ms(100);
    }
    sleep_ms(300);

    RetroiluminacaoPwm luz;
    PainelSt7789 painel;
    LedRgbAnodoComum led;
    EncoderKy040 encoder;
    Brilho brilho;
    g_led = &led;

    std::printf("\ncoruja - bancada do display ST7789V (GMT024-08-SPI8P)\n");

    if (kDiagnosticoCompleto) {
        rampa(luz);
        continuidade();
        varredura(painel);
        pisca_etapa(cores::kAzul, 4);
        anuncia("4/5 cores com INVON");
        mostra(painel, "vermelho", 0xF800);
        mostra(painel, "verde", 0x07E0);
        mostra(painel, "azul", 0x001F);
        orientacao(painel);
    } else {
        std::printf("diagnostico pulado (kDiagnosticoCompleto = false).\n");
        painel.inicia();
        luz.define_duty(brilho.duty());
    }

    telas_do_produto(painel, luz, encoder, brilho);
}
