// Junta o GPS com a base de pontos e imprime os alertas no console.
//
// **Não há reimplementação aqui.** O parser (`Nmea`) e a máquina de zonas
// (`Zonamento`) vêm do `coruja_nucleo`, o mesmo objeto que vai para o RP2350
// (ADR 0001). Se o alerta sair errado nesta tela, sai errado na estrada — que
// é exatamente o motivo de a validação valer alguma coisa. Este arquivo só
// cuida do que o firmware faz de outro jeito: abrir a serial e desenhar.
//
// Uso típico, a partir de `dispositivo/`:
//   simulador/simula_gps.py                       (num terminal)
//   firmware/build-host/ferramentas/monitor_alertas   (noutro)

#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "nucleo/BaseRadares.h"
#include "nucleo/Nmea.h"
#include "nucleo/Zonamento.h"

namespace {

using namespace coruja;

constexpr const char* kSerialPadrao = "simulador/serial";
constexpr const char* kBasePadrao = "servidor/dados/radares.bin";

/// Sem sentença válida por este tempo, o receptor é tratado como sem fix. São
/// quatro amostras perdidas a 4 Hz (RF01.4) — folga para um jitter de USB sem
/// deixar um alerta velho na tela.
constexpr std::uint32_t kSilencioMaxMs = 1000;

/// Uma linha NMEA cabe em 82 bytes por norma. O dobro é folga suficiente;
/// acima disso não é sentença, é lixo, e acumular só faria o buffer crescer
/// sem limite se o `\n` nunca chegasse.
constexpr std::size_t kLinhaMaxima = 164;

volatile std::sig_atomic_t g_parar = 0;
void ao_interromper(int) { g_parar = 1; }

// ------------------------------------------------------------------ desenho

struct Estilo {
    const char* cor;
    const char* rotulo;
    const char* led;
};

/// Cores e cadências do RF03, §"Codificação visual". Não são escolha de
/// console: são as mesmas do LED, para a tela e a placa dizerem a mesma coisa.
Estilo estilo_de(Zona z) {
    switch (z) {
        case Zona::SemSinal:            return {"\033[90m", "SEM SINAL", "apagado"};
        case Zona::Segura:              return {"\033[32m", "segura",    "verde"};
        case Zona::AproximacaoConforme: return {"\033[33m", "APROX",     "amarelo"};
        case Zona::Semaforo:            return {"\033[93m", "SEMAFORO",  "amarelo<->vermelho 2 Hz"};
        case Zona::AproximacaoMargem:   return {"\033[95m", "MARGEM",    "rosa 1 Hz"};
        case Zona::Perigo:              return {"\033[91m", "PERIGO",    "vermelho 4 Hz"};
    }
    return {"", "?", "?"};
}

class Tela {
public:
    explicit Tela(bool colorido) : colorido_(colorido) {}

    /// Evento: rola para cima e fica no histórico.
    void evento(std::uint32_t ms, const Veredito& v, float velocidade) {
        limpa_status();
        const Estilo e = estilo_de(v.zona);
        char alvo[40] = "—";
        if (v.tem_alvo) {
            std::snprintf(alvo, sizeof alvo, "%s %u", descreve(v.alvo.tipo),
                          static_cast<unsigned>(v.alvo.limite));
        }
        char dist[16] = "—";
        if (v.tem_alvo) {
            std::snprintf(dist, sizeof dist, "%.0f m", v.distancia_m);
        }
        std::printf("  %s   %s%-9s%s  %-22s %8s  %6.1f  %s\n", tempo(ms).c_str(),
                    cor(e.cor), e.rotulo, cor("\033[0m"), alvo, dist,
                    static_cast<double>(velocidade), descreve(v.faixa));
        std::fflush(stdout);
    }

    /// Status: sobrescrito a cada amostra, não entra no histórico.
    void status(const Veredito& v, float velocidade) {
        if (!colorido_) { return; }  // em pipe, só os eventos fazem sentido
        const Estilo e = estilo_de(v.zona);
        std::printf("\r\033[K  \033[1m%s%s\033[0m", e.cor, e.rotulo);
        if (v.tem_alvo) {
            std::printf("  %s %u · %.0f m · margem %.1f km/h",
                        descreve(v.alvo.tipo),
                        static_cast<unsigned>(v.alvo.limite),
                        static_cast<double>(v.distancia_m),
                        static_cast<double>(v.v_infra_kmh - velocidade));
        }
        std::printf("  ·  %.1f km/h  ·  LED %s", static_cast<double>(velocidade),
                    e.led);
        if (v.faixa != FaixaSonora::Nenhuma) {
            std::printf("  ·  buzzer %s", descreve(v.faixa));
        }
        std::fflush(stdout);
        status_visivel_ = true;
    }

    void limpa_status() {
        if (status_visivel_) {
            std::printf("\r\033[K");
            status_visivel_ = false;
        }
    }

private:
    const char* cor(const char* c) const { return colorido_ ? c : ""; }

    static std::string tempo(std::uint32_t ms) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%2u:%04.1f", ms / 60000U,
                      static_cast<double>(ms % 60000U) / 1000.0);
        return buf;
    }

    bool colorido_;
    bool status_visivel_ = false;
};

// ------------------------------------------------------------------- insumos

bool carrega_do_disco(const char* caminho, std::vector<Ponto>* destino) {
    std::FILE* f = std::fopen(caminho, "rb");
    if (f == nullptr) {
        std::fprintf(stderr, "nao abriu a base '%s': %s\n", caminho,
                     std::strerror(errno));
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long tamanho = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<std::uint8_t> bruto(static_cast<std::size_t>(tamanho));
    const bool leu = std::fread(bruto.data(), 1, bruto.size(), f) == bruto.size();
    std::fclose(f);
    if (!leu) {
        std::fprintf(stderr, "leitura incompleta de '%s'\n", caminho);
        return false;
    }

    destino->resize(kTetoPontos);
    const ResultadoCarga r =
        carrega_base(bruto.data(), bruto.size(), destino->data(), destino->size());
    if (!r.ok()) {
        std::fprintf(stderr, "base invalida: %s\n", descreve(r.erro));
        return false;
    }
    destino->resize(r.pontos);
    return true;
}

/// A busca binária do RF02.1 **assume** a base ordenada por latitude. Se o
/// conversor mudar e a ordem se perder, a máquina não acusa erro: ela apenas
/// deixa de achar pontos, em silêncio. Vale os 18 mil comparações do arranque.
bool esta_ordenada(const std::vector<Ponto>& base) {
    return std::is_sorted(base.begin(), base.end(),
                          [](const Ponto& a, const Ponto& b) {
                              return a.lat < b.lat;
                          });
}

speed_t converte_baud(long valor) {
    switch (valor) {
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return 0;
    }
}

/// Abre a serial. Com `--baud` configura a porta em modo cru; sem ele, **não
/// mexe** nas configurações — a pty do simulador já vem crua, e reconfigurar
/// por cima seria disputar o terminal com quem o criou.
int abre_serial(const char* caminho, long baud) {
    const int fd = ::open(caminho, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        std::fprintf(stderr, "nao abriu a serial '%s': %s\n", caminho,
                     std::strerror(errno));
        return -1;
    }
    if (baud == 0) { return fd; }

    const speed_t velocidade = converte_baud(baud);
    if (velocidade == 0) {
        std::fprintf(stderr, "baud nao suportado: %ld\n", baud);
        ::close(fd);
        return -1;
    }
    termios cfg{};
    if (::tcgetattr(fd, &cfg) != 0) {
        std::fprintf(stderr, "tcgetattr: %s\n", std::strerror(errno));
        ::close(fd);
        return -1;
    }
    ::cfmakeraw(&cfg);
    ::cfsetispeed(&cfg, velocidade);
    ::cfsetospeed(&cfg, velocidade);
    if (::tcsetattr(fd, TCSANOW, &cfg) != 0) {
        std::fprintf(stderr, "tcsetattr: %s\n", std::strerror(errno));
        ::close(fd);
        return -1;
    }
    return fd;
}

std::uint32_t agora_ms(std::chrono::steady_clock::time_point inicio) {
    using namespace std::chrono;
    return static_cast<std::uint32_t>(
        duration_cast<milliseconds>(steady_clock::now() - inicio).count());
}

struct Contagem {
    unsigned sentencas = 0;
    unsigned sem_fix = 0;
    unsigned erros = 0;
    unsigned eventos = 0;
};

}  // namespace

int main(int argc, char** argv) {
    const char* caminho_serial = kSerialPadrao;
    const char* caminho_base = kBasePadrao;
    long baud = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "--serial" || a == "--base" || a == "--baud") && i + 1 >= argc) {
            std::fprintf(stderr, "%s exige um valor\n", a.c_str());
            return 2;
        }
        if (a == "--serial") { caminho_serial = argv[++i]; }
        else if (a == "--base") { caminho_base = argv[++i]; }
        else if (a == "--baud") { baud = std::strtol(argv[++i], nullptr, 10); }
        else {
            std::printf("uso: %s [--serial CAMINHO] [--base CAMINHO] [--baud N]\n",
                        argv[0]);
            return a == "--ajuda" || a == "-h" ? 0 : 2;
        }
    }

    std::vector<Ponto> base;
    if (!carrega_do_disco(caminho_base, &base)) { return 1; }
    const bool ordenada = esta_ordenada(base);

    const int fd = abre_serial(caminho_serial, baud);
    if (fd < 0) {
        std::fprintf(stderr,
                     "\ndica: o simulador cria o atalho ao subir.\n"
                     "      simulador/simula_gps.py\n");
        return 1;
    }

    const bool colorido = ::isatty(STDOUT_FILENO) != 0;
    Tela tela(colorido);
    std::printf("coruja · monitor de alertas\n");
    std::printf("  base   : %s — %zu pontos, %s\n", caminho_base, base.size(),
                ordenada ? "ordenada por latitude" : "\033[91mFORA DE ORDEM\033[0m");
    if (!ordenada) {
        std::printf("           a busca do RF02.1 depende da ordem e vai "
                    "perder pontos em silencio\n");
    }
    std::printf("  serial : %s%s\n", caminho_serial,
                baud != 0 ? " (reconfigurada)" : "");
    std::printf("  ctrl-c encerra\n\n");
    std::printf("    tempo   zona       alvo                       dist     "
                "vel  buzzer\n");
    std::printf("  ----------------------------------------------------------"
                "-------------\n");

    std::signal(SIGINT, ao_interromper);
    const auto inicio = std::chrono::steady_clock::now();

    MaquinaZona maquina;
    Veredito anterior;
    bool teve_anterior = false;
    std::string pendente;
    std::uint32_t ultimo_valido = 0;
    bool ja_teve_fix = false;
    float velocidade = 0.0F;
    Contagem conta;

    while (g_parar == 0) {
        fd_set leitura;
        FD_ZERO(&leitura);
        FD_SET(fd, &leitura);
        timeval espera{0, 100000};  // 100 ms: ritmo do relógio, não dos dados
        const int pronto = ::select(fd + 1, &leitura, nullptr, nullptr, &espera);
        const std::uint32_t t = agora_ms(inicio);

        if (pronto > 0) {
            char bloco[512];
            const ssize_t lidos = ::read(fd, bloco, sizeof bloco);
            if (lidos == 0 || (lidos < 0 && errno == EIO)) {
                tela.limpa_status();
                std::printf("\n  o outro lado da serial fechou.\n");
                break;
            }
            if (lidos > 0) { pendente.append(bloco, static_cast<std::size_t>(lidos)); }
        }

        // --- extrai linhas completas ---
        std::size_t corte;
        while ((corte = pendente.find('\n')) != std::string::npos) {
            std::string linha = pendente.substr(0, corte);
            pendente.erase(0, corte + 1);
            if (!linha.empty() && linha.back() == '\r') { linha.pop_back(); }
            if (linha.empty()) { continue; }

            Telemetria tel;
            const ErroNmea e = analisa_rmc(linha.c_str(), linha.size(), &tel);
            if (e == ErroNmea::NaoEhRmc) { continue; }  // GGA, GSV, VTG...
            ++conta.sentencas;

            if (e == ErroNmea::SemFix) {
                ++conta.sem_fix;
                const Veredito v = maquina.sem_fix(t);
                if (!teve_anterior || v.zona != anterior.zona) {
                    tela.evento(t, v, velocidade);
                    ++conta.eventos;
                }
                anterior = v;
                teve_anterior = true;
                continue;
            }
            if (e != ErroNmea::Nenhum) {
                ++conta.erros;
                continue;
            }

            velocidade = tel.velocidade_kmh;
            ultimo_valido = t;
            ja_teve_fix = true;
            const Veredito v = maquina.avalia(tel, base.data(), base.size(), t);

            // Evento quando muda a zona, o alvo **ou a faixa sonora**. Trocar
            // de radar dentro da mesma zona é informação, não ruído — e a
            // faixa também: ela é o que o motorista de fato ouve, e dentro
            // da Zona de Perigo ela muda sem a zona mudar. Sem esta terceira
            // condição, subir de 70 para 80 km/h em cima de um radar não
            // produz linha nenhuma, e a validação não enxerga a passagem de
            // `lenta` para `pulso`.
            const bool trocou_alvo =
                v.tem_alvo != anterior.tem_alvo ||
                (v.tem_alvo && (v.alvo.lat != anterior.alvo.lat ||
                                v.alvo.lon != anterior.alvo.lon));
            if (!teve_anterior || v.zona != anterior.zona || trocou_alvo ||
                v.faixa != anterior.faixa) {
                tela.evento(t, v, velocidade);
                ++conta.eventos;
            }
            anterior = v;
            teve_anterior = true;
            tela.status(v, velocidade);
        }

        if (pendente.size() > kLinhaMaxima) { pendente.clear(); }

        // --- silêncio prolongado vira "sem fix", sem esperar o receptor dizer ---
        if (ja_teve_fix && t - ultimo_valido > kSilencioMaxMs) {
            const Veredito v = maquina.sem_fix(t);
            if (anterior.zona != Zona::SemSinal) {
                tela.evento(t, v, velocidade);
                ++conta.eventos;
            }
            anterior = v;
            teve_anterior = true;
            tela.status(v, velocidade);
        }
    }

    tela.limpa_status();
    ::close(fd);
    std::printf("\n  %u sentencas RMC · %u eventos · %u sem fix · %u com erro\n",
                conta.sentencas, conta.eventos, conta.sem_fix, conta.erros);
    return 0;
}
