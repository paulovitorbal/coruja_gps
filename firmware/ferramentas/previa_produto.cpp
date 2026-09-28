// O produto inteiro rodando no host: GPS do simulador, zonas, LED, buzzer e
// tela — com o código que vai para a placa.
//
// **Nada aqui é maquete.** `LeitorGps`, `PilotoAlerta`, `MaquinaZona`,
// `TelaPrincipal`, `CadenciaBuzzer` e `PadraoLed` são os mesmos objetos que
// o `main.cpp` vai instanciar. O que muda são os três portes de saída: a
// UART vira uma pty, o painel vira ANSI, e o LED e o buzzer viram texto.
//
// Uso, a partir de `dispositivo/`:
//   simulador/simula_gps.py --sem-pausa       (num terminal)
//   firmware/build-host/ferramentas/previa_produto   (noutro)

#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "VisorTerminal.h"
#include "app/PilotoAlerta.h"
#include "display/Brilho.h"
#include "display/TelaPrincipal.h"
#include "nucleo/BaseRadares.h"

namespace {

using namespace coruja;

volatile std::sig_atomic_t g_parar = 0;
void ao_interromper(int) { g_parar = 1; }

/// A UART do host: a pty que o simulador cria.
class UartPosix : public Uart {
public:
    explicit UartPosix(int fd) : fd_(fd) {}
    void escreve(const std::uint8_t* b, std::size_t n) override {
        if (::write(fd_, b, n) < 0) { /* a prévia não configura o módulo */ }
    }
    std::size_t le(std::uint8_t* destino, std::size_t capacidade) override {
        const ssize_t n = ::read(fd_, destino, capacidade);
        return n > 0 ? static_cast<std::size_t>(n) : 0;
    }
    void define_baud(std::uint32_t) override {}

private:
    int fd_;
};

/// LED e buzzer viram texto no rodapé — é o canal que o terminal tem.
class LedTexto : public LedRgb {
public:
    void define_cor(const Cor& c) override { atual_ = c; }
    Cor cor_atual() const override { return atual_; }
    std::string nome() const {
        if (atual_ == cores::kApagado)  { return "apagado"; }
        if (atual_ == cores::kVerde)    { return "VERDE"; }
        if (atual_ == cores::kAmarelo)  { return "AMARELO"; }
        if (atual_ == cores::kRosa)     { return "ROSA"; }
        if (atual_ == cores::kVermelho) { return "VERMELHO"; }
        return "?";
    }
private:
    Cor atual_ = cores::kApagado;
};

class BuzzerTexto : public Buzzer {
public:
    void define(bool l) override { ligado_ = l; }
    bool ligado() const override { return ligado_; }
private:
    bool ligado_ = false;
};

std::uint32_t agora_ms(std::chrono::steady_clock::time_point inicio) {
    using namespace std::chrono;
    return static_cast<std::uint32_t>(
        duration_cast<milliseconds>(steady_clock::now() - inicio).count());
}

bool carrega(const char* caminho, std::vector<Ponto>* destino) {
    std::FILE* f = std::fopen(caminho, "rb");
    if (f == nullptr) { return false; }
    std::fseek(f, 0, SEEK_END);
    const long tam = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<std::uint8_t> bruto(static_cast<std::size_t>(tam));
    const bool leu = std::fread(bruto.data(), 1, bruto.size(), f) == bruto.size();
    std::fclose(f);
    if (!leu) { return false; }
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
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--serial" && i + 1 < argc) { caminho_serial = argv[++i]; }
        else if (a == "--base" && i + 1 < argc) { caminho_base = argv[++i]; }
        else {
            std::printf("uso: %s [--serial CAMINHO] [--base CAMINHO]\n", argv[0]);
            return 2;
        }
    }

    std::vector<Ponto> base;
    if (!carrega(caminho_base, &base)) {
        std::fprintf(stderr, "nao deu para carregar '%s'\n", caminho_base);
        return 1;
    }

    const int fd = ::open(caminho_serial, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        std::fprintf(stderr, "nao abriu '%s': %s\n\ndica: suba o simulador "
                     "antes:\n  simulador/simula_gps.py --sem-pausa\n",
                     caminho_serial, std::strerror(errno));
        return 1;
    }

    UartPosix     uart(fd);
    LeitorGps     leitor(uart);
    LedTexto      led;
    BuzzerTexto   buzzer;
    PilotoAlerta  piloto(leitor, led, buzzer);
    TelaPrincipal tela;
    VisorTerminal visor;
    Brilho        brilho;

    piloto.define_base(base.data(), base.size());
    std::signal(SIGINT, ao_interromper);
    const auto inicio = std::chrono::steady_clock::now();
    std::uint32_t sem_sinal_desde = 0;
    bool estava_sem_sinal = true;

    std::printf("\033[2J\033[H");
    while (g_parar == 0) {
        const std::uint32_t t = agora_ms(inicio);
        piloto.passo(t);

        const bool tem = leitor.tem_fix(t);
        if (!tem && !estava_sem_sinal) { sem_sinal_desde = t; }
        estava_sem_sinal = !tem;

        EstadoTela estado;
        estado.veredito = piloto.veredito();
        estado.telemetria = leitor.telemetria();
        estado.tem_fix = tem;
        estado.base_disponivel = !base.empty();
        estado.taxa = leitor.monitor().estado();
        estado.sem_sinal_desde_ms = sem_sinal_desde;
        estado.brilho_pct = brilho.percentual();

        char rodape[200];
        std::snprintf(rodape, sizeof rodape,
                      " LED %-9s buzzer %-3s | zona %-20s | %.0f km/h "
                      "%.5f,%.5f | alvo %s | taxa %.1f Hz | %u fixes",
                      led.nome().c_str(), buzzer.ligado() ? "ON" : "--",
                      descreve(piloto.veredito().zona),
                      static_cast<double>(leitor.telemetria().velocidade_kmh),
                      static_cast<double>(leitor.telemetria().lat),
                      static_cast<double>(leitor.telemetria().lon),
                      piloto.veredito().tem_alvo ? "sim" : "nao",
                      static_cast<double>(leitor.monitor().taxa_hz()),
                      leitor.fixes());
        visor.define_rodape(rodape);
        tela.desenha(estado, t, visor);
        // O painel não se redesenha quando nada muda, mas o rodapé sim: ele
        // carrega o LED e o buzzer, que pulsam.
        visor.apresenta();

        ::usleep(50000);   // 20 Hz de atualização de tela
    }
    ::close(fd);
    std::printf("\033[0m\n");
    return 0;
}
