// O produto inteiro rodando no host — pela `Aplicacao`, não por peças soltas.
//
// **Nada aqui é maquete.** `Aplicacao`, `DiarioBordo`, `MaquinaZona`,
// `MenuAjustes`, as telas, o `PilotoAlerta` e o `CadenciaBuzzer` são os
// mesmos objetos que o `main.cpp` instancia. O que muda são os portes de
// borda, todos em `PortesHost`: a UART vira uma pty, o cartão vira um
// diretório, o encoder vira o teclado, e o LED e o buzzer viram texto.
//
// ## Por que passar pela `Aplicacao` e não montar as peças à mão
//
// A versão anterior desta ferramenta fiava `PilotoAlerta` e `TelaPrincipal`
// direto, sem a `Aplicacao`. Funcionava para ver alerta e tela — mas deixava
// de fora tudo que vive na `Aplicacao`: o menu, o `DetectorParado`, a
// gravação de ajustes e, desde 2026-10-04, o `DiarioBordo`.
//
// A consequência foi concreta: `infracoes.log` e o arquivo de viagem **nunca
// haviam sido produzidos pelo caminho de código real** — só por dublês em
// memória, nos testes unitários. A primeira integração de verdade seria no
// carro, que é o lugar mais caro possível para achar um defeito de formato.
//
// Uso, a partir de `dispositivo/`:
//   simulador/simula_gps.py --sem-pausa          (num terminal)
//   firmware/build-host/ferramentas/previa_produto --cartao /tmp/coruja
//
// Teclas: `a` e `d` giram o encoder, espaço clica, `q` sai.

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "PortesHost.h"
#include "VisorTerminal.h"
#include "app/Aplicacao.h"
#include "app/PilotoAlerta.h"
#include "display/Brilho.h"
#include "log/LoggerConsole.h"
#include "nucleo/BaseRadares.h"
#include "nucleo/VerificadorDownload.h"

namespace {

using namespace coruja;

volatile std::sig_atomic_t g_parar = 0;
void ao_interromper(int) { g_parar = 1; }

std::uint32_t agora_ms(std::chrono::steady_clock::time_point inicio) {
    using namespace std::chrono;
    return static_cast<std::uint32_t>(
        duration_cast<milliseconds>(steady_clock::now() - inicio).count());
}

/// Carrega a base e devolve o cabeçalho junto — a tela de informação mostra
/// a data dele, e sem isso a prévia não exercitaria essa linha.
bool carrega(const char* caminho, std::vector<Ponto>* destino,
             CabecalhoBase* cabecalho) {
    std::FILE* f = std::fopen(caminho, "rb");
    if (f == nullptr) { return false; }
    std::fseek(f, 0, SEEK_END);
    const long tam = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<std::uint8_t> bruto(static_cast<std::size_t>(tam));
    const bool leu =
        std::fread(bruto.data(), 1, bruto.size(), f) == bruto.size();
    std::fclose(f);
    if (!leu || bruto.size() < kTamCabecalhoSemData) { return false; }

    *cabecalho = le_cabecalho(bruto.data());
    destino->resize(kTetoPontos);
    const ResultadoCarga r = carrega_base(bruto.data(), bruto.size(),
                                          destino->data(), destino->size());
    if (!r.ok()) {
        std::fprintf(stderr, "base invalida: %s\n", descreve(r.erro));
        return false;
    }
    destino->resize(r.pontos);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const char* caminho_serial = "simulador/serial";
    const char* caminho_base = "servidor/dados/radares.bin";
    const char* caminho_cartao = "/tmp/coruja_previa";
    bool viagem_automatica = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--serial" && i + 1 < argc)      { caminho_serial = argv[++i]; }
        else if (a == "--base" && i + 1 < argc)   { caminho_base = argv[++i]; }
        else if (a == "--cartao" && i + 1 < argc) { caminho_cartao = argv[++i]; }
        else if (a == "--viagem") { viagem_automatica = true; }
        else {
            std::printf("uso: %s [--serial C] [--base C] [--cartao DIR] "
                        "[--viagem]\n",
                        argv[0]);
            return 2;
        }
    }

    std::vector<Ponto> base;
    CabecalhoBase cabecalho{};
    if (!carrega(caminho_base, &base, &cabecalho)) {
        std::fprintf(stderr, "nao deu para carregar '%s'\n", caminho_base);
        return 1;
    }

    ::mkdir(caminho_cartao, 0755);

    const int fd = ::open(caminho_serial, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        std::fprintf(stderr, "nao abriu '%s': %s\n\ndica: suba o simulador "
                     "antes:\n  simulador/simula_gps.py --sem-pausa\n",
                     caminho_serial, std::strerror(errno));
        return 1;
    }

    host::UartPty              uart(fd);
    LeitorGps                  leitor(uart);
    host::LedTexto             led;
    host::BuzzerTexto          buzzer;
    PilotoAlerta               piloto(leitor, led, buzzer);
    VisorTerminal              visor;
    Brilho                     brilho;
    host::EncoderTeclado       encoder;
    host::ArmazenamentoArquivo cartao(caminho_cartao);
    host::AcoesTexto           acoes;
    LoggerConsole              log;
    Configuracao               config;
    static char                trabalho[4096];

    std::snprintf(config.nome, sizeof config.nome, "previa");
    piloto.define_base(base.data(), base.size());

    Aplicacao app(leitor, encoder, piloto, brilho, cartao, acoes, log, config,
                  trabalho, sizeof trabalho, &visor);
    app.define_versao("previa-host");
    app.define_base_carregada(cabecalho, base.size());
    if (viagem_automatica) {
        // Sem passar pelo menu: a previa roda com o veiculo em movimento, e
        // o menu exige o `DetectorParado`. A navegacao tem suite propria.
        app.alterna_viagem();
    }

    std::signal(SIGINT, ao_interromper);
    const auto inicio = std::chrono::steady_clock::now();

    std::printf("\033[2J\033[H");
    while (g_parar == 0) {
        const std::uint32_t t = agora_ms(inicio);
        app.passo(t);

        const auto& tel = leitor.telemetria();
        char rodape[240];
        std::snprintf(rodape, sizeof rodape,
                      " LED %-9s buz %-3s | %-20s | %.0f km/h %.5f,%.5f | "
                      "viagem %-10s %.2f km | escritas %zu | a/d gira, espaco clica",
                      led.nome().c_str(), buzzer.ligado() ? "ON" : "--",
                      descreve(piloto.veredito().zona),
                      static_cast<double>(tel.velocidade_kmh),
                      static_cast<double>(tel.lat),
                      static_cast<double>(tel.lon),
                      descreve(app.estado_viagem()),
                      static_cast<double>(app.dist_viagem_km()),
                      cartao.escritas());
        visor.define_rodape(rodape);
        visor.apresenta();

        ::usleep(50000);   // 20 Hz
    }
    ::close(fd);
    std::printf("\033[0m\n\ncartao da previa: %s\n", caminho_cartao);
    return 0;
}
