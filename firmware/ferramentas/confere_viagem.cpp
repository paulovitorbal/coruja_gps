// =============================================================================
//  confere_viagem — roda o avaliador REAL sobre um log de viagem gravado
//
//  Existe por causa de 08/10/2026: a viagem registrou `n_radares=0` nos 192
//  pontos, e uma conferência em Python disse que sete deles tinham radar
//  válido na janela. Duas explicações cabiam — o firmware erra, ou a
//  conferência erra — e nenhuma reimplementação pode decidir isso.
//
//  Esta ferramenta não reimplementa nada: carrega a base com `carrega_base` e
//  avalia com `MaquinaZona::avalia`, o mesmo código que roda no aparelho.
//
//    confere_viagem <viagem.log> <radares.bin>
//
//  ⚠️ O log de viagem traz posições reais. A saída é agregada de propósito:
//  contagens e distâncias, nunca uma coordenada.
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "nucleo/BaseRadares.h"
#include "nucleo/CarregadorFluxo.h"
#include "app/PilotoAlerta.h"
#include "buzzer/Buzzer.h"
#include "gps/LeitorGps.h"
#include "gps/Uart.h"
#include "led/LedRgb.h"
#include "nucleo/Zonamento.h"

namespace {

/// Uma linha de viagem v3, só o que o avaliador consome.
struct Amostra {
    coruja::Telemetria t;
    bool valida = false;
};

Amostra le_linha(char* linha) {
    Amostra a;
    // utc;lat;lon;v_media;dist_km;radar_m;radar_kmh;perto_m;perto_kmh;rumo;...
    // Divisão manual: `strtok` COLAPSA delimitadores consecutivos, e a linha
    // de viagem é cheia de campos vazios (`;;;`) porque campo vazio não é
    // zero — é "não havia radar". Com `strtok` as colunas escorregam e a
    // leitura silenciosamente ignora a maioria das linhas.
    const char* campo[12] = {nullptr};
    int n = 0;
    char* p = linha;
    campo[n++] = p;
    for (; *p != '\0' && n < 12; ++p) {
        if (*p == ';') {
            *p = '\0';
            campo[n++] = p + 1;
        } else if (*p == '\n' || *p == '\r') {
            *p = '\0';
            break;
        }
    }
    for (char* q = p; *q != '\0'; ++q) {
        if (*q == '\n' || *q == '\r') { *q = '\0'; break; }
    }
    if (n < 10 || campo[0] == nullptr || campo[0][0] != '2') { return a; }
    if (campo[1] == nullptr || campo[1][0] == '\0') { return a; }
    a.t.lat = std::strtof(campo[1], nullptr);
    a.t.lon = std::strtof(campo[2], nullptr);
    a.t.velocidade_kmh = std::strtof(campo[3], nullptr);
    if (campo[9] != nullptr && campo[9][0] != '\0') {
        a.t.rumo_graus = std::strtof(campo[9], nullptr);
        a.t.rumo_valido = true;
    }
    // "2026-10-08T15:32:06Z" -> hora/minuto/segundo
    if (std::strlen(campo[0]) >= 19) {
        a.t.hora = static_cast<std::uint8_t>(std::atoi(campo[0] + 11));
        a.t.minuto = static_cast<std::uint8_t>(std::atoi(campo[0] + 14));
        a.t.segundo = static_cast<std::uint8_t>(std::atoi(campo[0] + 17));
    }
    a.t.data_valida = true;
    a.valida = true;
    return a;
}

}  // namespace

/// UART que devolve sentenças RMC montadas a partir do log de viagem.
///
/// É o caminho que o aparelho percorre de verdade — `LeitorGps` monta a
/// telemetria a partir dos bytes, e não de uma struct preenchida à mão. Se a
/// diferença estiver entre a RMC e o `Veredito`, só aparece aqui.
class UartDeLog : public coruja::Uart {
public:
    void entrega(const std::string& frase) { pendente_ += frase; }
    void escreve(const std::uint8_t*, std::size_t) override {}
    std::size_t le(std::uint8_t* destino, std::size_t capacidade) override {
        const std::size_t n =
            pendente_.size() < capacidade ? pendente_.size() : capacidade;
        std::memcpy(destino, pendente_.data(), n);
        pendente_.erase(0, n);
        return n;
    }
    void define_baud(std::uint32_t) override {}

private:
    std::string pendente_;
};

class LedMudo : public coruja::LedRgb {
public:
    void define_cor(const coruja::Cor& c) override { cor_ = c; }
    coruja::Cor cor_atual() const override { return cor_; }
private:
    coruja::Cor cor_{};
};

class BuzzerMudo : public coruja::Buzzer {
public:
    void define(bool l) override { ligado_ = l; }
    bool ligado() const override { return ligado_; }
private:
    bool ligado_ = false;
};

/// Monta uma `$GNRMC` válida, com checksum, a partir de uma amostra.
std::string monta_rmc(const coruja::Telemetria& t) {
    auto graus_min = [](float g, int largura) {
        const float a = g < 0 ? -g : g;
        const int inteiro = static_cast<int>(a);
        const double minutos = (a - inteiro) * 60.0;
        char buf[32];
        std::snprintf(buf, sizeof buf, "%0*d%08.5f", largura, inteiro, minutos);
        return std::string(buf);
    };
    char corpo[160];
    std::snprintf(corpo, sizeof corpo,
                  "GNRMC,%02u%02u%02u.00,A,%s,%c,%s,%c,%.2f,%.1f,0801026,,,A",
                  static_cast<unsigned>(t.hora), static_cast<unsigned>(t.minuto),
                  static_cast<unsigned>(t.segundo),
                  graus_min(t.lat, 2).c_str(), t.lat < 0 ? 'S' : 'N',
                  graus_min(t.lon, 3).c_str(), t.lon < 0 ? 'W' : 'E',
                  static_cast<double>(t.velocidade_kmh) / 1.852,
                  static_cast<double>(t.rumo_graus));
    std::uint8_t ck = 0;
    for (const char* p = corpo; *p != '\0'; ++p) {
        ck = static_cast<std::uint8_t>(ck ^ static_cast<std::uint8_t>(*p));
    }
    char linha[200];
    std::snprintf(linha, sizeof linha, "$%s*%02X\r\n", corpo, ck);
    return std::string(linha);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "uso: confere_viagem <viagem.log> <radares.bin>\n");
        return 2;
    }

    std::FILE* fb = std::fopen(argv[2], "rb");
    if (fb == nullptr) { std::perror("radares.bin"); return 2; }
    std::fseek(fb, 0, SEEK_END);
    const long tam = std::ftell(fb);
    std::fseek(fb, 0, SEEK_SET);
    std::vector<std::uint8_t> bruto(static_cast<std::size_t>(tam));
    if (std::fread(bruto.data(), 1, bruto.size(), fb) != bruto.size()) {
        std::fprintf(stderr, "leitura curta da base\n");
        return 2;
    }
    std::fclose(fb);

    std::vector<coruja::Ponto> pontos(40000);
    const auto r = coruja::carrega_base(bruto.data(), bruto.size(),
                                        pontos.data(), pontos.size());
    if (r.erro != coruja::ErroBase::Nenhum) {
        std::fprintf(stderr, "base recusada: erro %d\n",
                     static_cast<int>(r.erro));
        return 1;
    }
    std::printf("carga por BUFFER: %zu pontos\n", r.pontos);

    // O aparelho não usa este caminho: ele lê o cartão em pedaços de 4096 B
    // e alimenta o `CarregadorFluxo`. Doze não divide 4096, então registros
    // partem entre pedaços — e são dois decodificadores diferentes.
    std::vector<coruja::Ponto> fluxo_pontos(coruja::kCapacidadeFirmware);
    coruja::CarregadorFluxo cf(fluxo_pontos.data(), fluxo_pontos.size());
    for (std::size_t off = 0; off < bruto.size(); off += 4096) {
        const std::size_t n = (bruto.size() - off) < 4096 ? (bruto.size() - off)
                                                          : 4096;
        cf.alimenta(bruto.data() + off, n);
    }
    const auto erro_fluxo = cf.conclui();
    std::printf("carga por FLUXO (4096 B, como o cartao): %zu pontos, erro %d\n",
                cf.pontos(), static_cast<int>(erro_fluxo));

    std::size_t divergentes = 0;
    const std::size_t menor_n = cf.pontos() < r.pontos ? cf.pontos() : r.pontos;
    for (std::size_t i = 0; i < menor_n; ++i) {
        const auto& a = pontos[i];
        const auto& b = fluxo_pontos[i];
        if (a.lat != b.lat || a.lon != b.lon || a.limite != b.limite ||
            a.rumo_q != b.rumo_q || a.sentido != b.sentido || a.tipo != b.tipo) {
            ++divergentes;
        }
    }
    std::printf("pontos divergentes entre os dois caminhos: %zu\n", divergentes);

    std::FILE* fv = std::fopen(argv[1], "r");
    if (fv == nullptr) { std::perror("viagem"); return 2; }

    coruja::MaquinaZona maquina;
    char linha[256];
    std::uint32_t agora_ms = 0;
    unsigned n_amostras = 0, com_candidato = 0, com_alvo = 0;
    unsigned por_zona[6] = {0, 0, 0, 0, 0, 0};
    float menor = 1e9F;
    std::uint8_t limite_menor = 0;

    while (std::fgets(linha, sizeof linha, fv) != nullptr) {
        const Amostra a = le_linha(linha);
        if (!a.valida) { continue; }
        ++n_amostras;
        agora_ms += 6000;            // a fatia do log é de 6 s
        const coruja::Veredito v =
            maquina.avalia(a.t, pontos.data(), r.pontos, agora_ms);
        const auto z = static_cast<unsigned>(v.zona);
        if (z < 6) { ++por_zona[z]; }
        if (v.n_candidatos > 0) { ++com_candidato; }
        if (v.tem_alvo) {
            ++com_alvo;
            // Minuto e segundo bastam para situar na viagem sem publicar onde.
            std::printf("  alerta em %02u:%02u  v=%.0f km/h  limite %u  "
                        "%.0f m  candidatos %u\n",
                        static_cast<unsigned>(a.t.minuto),
                        static_cast<unsigned>(a.t.segundo),
                        static_cast<double>(a.t.velocidade_kmh),
                        static_cast<unsigned>(v.alvo.limite),
                        static_cast<double>(v.distancia_m),
                        static_cast<unsigned>(v.n_candidatos));
        }
        if (v.tem_mais_proximo && v.dist_mais_proximo_m < menor) {
            menor = v.dist_mais_proximo_m;
            limite_menor = v.mais_proximo.limite;
        }
    }
    std::fclose(fv);

    std::printf("amostras avaliadas: %u\n", n_amostras);
    std::printf("com n_candidatos > 0: %u\n", com_candidato);
    std::printf("com alvo (alerta): %u\n", com_alvo);
    if (menor < 1e8F) {
        std::printf("aproximacao minima vista pelo avaliador: %.0f m "
                    "(limite %u)\n", static_cast<double>(menor),
                    static_cast<unsigned>(limite_menor));
    }
    // ---- segunda passada: o caminho COMPLETO, da UART ao veredito ----
    //
    // A primeira passada chama `avalia` direto. Esta passa por `LeitorGps` e
    // `PilotoAlerta`, que é o que o aparelho faz. Divergência entre as duas
    // localiza o defeito no elo, e não no avaliador.
    {
        std::FILE* f2 = std::fopen(argv[1], "r");
        UartDeLog uart;
        coruja::LeitorGps leitor(uart);
        LedMudo led;
        BuzzerMudo buzzer;
        coruja::PilotoAlerta piloto(leitor, led, buzzer);
        piloto.define_base(pontos.data(), r.pontos);

        std::uint32_t ms = 0;
        unsigned cheio = 0, com_fix = 0, zona_segura = 0, sem_sinal = 0;
        while (std::fgets(linha, sizeof linha, f2) != nullptr) {
            const Amostra a = le_linha(linha);
            if (!a.valida) { continue; }
            uart.entrega(monta_rmc(a.t));
            ms += 250;
            piloto.passo(ms);
            if (leitor.tem_fix(ms)) { ++com_fix; }
            const auto& v = piloto.veredito();
            if (v.n_candidatos > 0) { ++cheio; }
            if (v.zona == coruja::Zona::Segura) { ++zona_segura; }
            if (v.zona == coruja::Zona::SemSinal) { ++sem_sinal; }
        }
        std::fclose(f2);
        std::printf("\n-- caminho completo (UART -> LeitorGps -> Piloto) --\n");
        std::printf("com fix: %u | n_candidatos>0: %u | segura: %u | "
                    "sem sinal: %u\n", com_fix, cheio, zona_segura, sem_sinal);
    }

    static const char* kNomes[6] = {"sem sinal", "segura", "conforme",
                                    "semaforo", "margem", "perigo"};
    for (unsigned i = 0; i < 6; ++i) {
        if (por_zona[i] > 0) {
            std::printf("  zona %s: %u\n", kNomes[i], por_zona[i]);
        }
    }
    return 0;
}
