#include "app/PilotoAlerta.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "nucleo/Geo.h"

namespace {

using namespace coruja;

constexpr float kLat0 = -15.79F;
constexpr float kLon0 = -47.88F;

/// Entrega sentenças NMEA montadas na hora, para o teste poder pilotar
/// posição e velocidade como o simulador de bancada faz.
class SimuladorUart : public Uart {
public:
    void emite_rmc(float lat, float lon, float kmh, float rumo) {
        const char ns = lat < 0 ? 'S' : 'N';
        const char ew = lon < 0 ? 'W' : 'E';
        const float alat = lat < 0 ? -lat : lat;
        const float alon = lon < 0 ? -lon : lon;
        const int glat = static_cast<int>(alat);
        const int glon = static_cast<int>(alon);
        char corpo[128];
        std::snprintf(corpo, sizeof corpo,
                      "$GNRMC,123519.00,A,%02d%09.6f,%c,%03d%09.6f,%c,"
                      "%.2f,%.1f,220926,,,A",
                      glat, (alat - glat) * 60.0, ns,
                      glon, (alon - glon) * 60.0, ew,
                      static_cast<double>(kmh / 1.852F),
                      static_cast<double>(rumo));
        std::uint8_t cs = 0;
        for (const char* p = corpo + 1; *p != 0; ++p) {
            cs ^= static_cast<std::uint8_t>(*p);
        }
        char fim[8];
        std::snprintf(fim, sizeof fim, "*%02X\r\n", cs);
        const std::string s = std::string(corpo) + fim;
        saida.insert(saida.end(), s.begin(), s.end());
    }

    void escreve(const std::uint8_t*, std::size_t) override {}
    void define_baud(std::uint32_t) override {}
    std::size_t le(std::uint8_t* destino, std::size_t capacidade) override {
        const std::size_t n = saida.size() < capacidade ? saida.size()
                                                        : capacidade;
        for (std::size_t i = 0; i < n; ++i) { destino[i] = saida[i]; }
        saida.erase(saida.begin(), saida.begin() + static_cast<long>(n));
        return n;
    }
    std::vector<std::uint8_t> saida;
};

class LedEspiao : public LedRgb {
public:
    void define_cor(const Cor& c) override { atual = c; ++trocas; }
    Cor cor_atual() const override { return atual; }
    Cor atual = cores::kApagado;
    unsigned trocas = 0;
};

class BuzzerEspiao : public Buzzer {
public:
    void define(bool l) override {
        if (l) { ++ms_ligado; }
        ligado_ = l;
    }
    bool ligado() const override { return ligado_; }
    bool ligado_ = false;
    unsigned ms_ligado = 0;
};

Ponto radar(float norte_m, std::uint8_t limite) {
    Ponto p{};
    p.lat = kLat0 + norte_m / geo::kMetrosPorGrauLat;
    p.lon = kLon0;
    p.limite = limite;
    p.rumo_q = 0;
    p.tipo = (limite == kSemLimite) ? TipoPonto::SemaforoCamera
                                    : TipoPonto::RadarFixo;
    p.sentido = Sentido::Omnidirecional;
    return p;
}

struct Bancada {
    SimuladorUart uart;
    LeitorGps     gps{uart};
    LedEspiao     led;
    BuzzerEspiao  buzzer;
    PilotoAlerta  piloto{gps, led, buzzer};
    std::vector<Ponto> base;

    void com_radar(float norte_m, std::uint8_t limite) {
        base.push_back(radar(norte_m, limite));
        piloto.define_base(base.data(), base.size());
    }
    /// Roda como o firmware: uma sentença a cada 250 ms, e o laço girando
    /// mais rápido que isso.
    void dirige(float kmh, std::uint32_t de, std::uint32_t ate,
                float lat = kLat0) {
        for (std::uint32_t t = de; t <= ate; t += 50) {
            if (t % 250 == 0) { uart.emite_rmc(lat, kLon0, kmh, 0.0F); }
            piloto.passo(t);
        }
    }
};

// ============================================================ sem sinal

TEST(PilotoAlerta, sem_fix_apaga_o_led_e_cala_o_buzzer) {
    Bancada b;
    b.com_radar(100.0F, 60);
    for (std::uint32_t t = 0; t <= 1000; t += 50) { b.piloto.passo(t); }
    EXPECT_EQ(b.piloto.veredito().zona, Zona::SemSinal);
    EXPECT_EQ(b.led.atual, cores::kAzul);
    EXPECT_FALSE(b.buzzer.ligado());
}

TEST(PilotoAlerta, sem_base_carregada_tambem_e_sem_sinal) {
    // Cartao ausente ou base recusada: nao ha o que afirmar sobre a via, e o
    // motorista nao precisa saber qual dos dois faltou para entender isso.
    Bancada b;
    b.dirige(60.0F, 0, 1000);
    EXPECT_EQ(b.piloto.veredito().zona, Zona::SemSinal);
    EXPECT_EQ(b.led.atual, cores::kAzul);
}

TEST(PilotoAlerta, base_ENTREGUE_VAZIA_e_sem_sinal_e_nao_via_livre) {
    // Ponteiro válido, contagem ZERO. A guarda de `passo()` olhava só o
    // ponteiro, e `g_pontos` é um array global — endereço nunca nulo. Então
    // uma base vazia atravessava a guarda, o laço não varria nada, e o
    // veredito saía `Segura`: o aparelho AFIRMANDO "nenhum radar a menos de
    // 300 m" quando a verdade era "não tenho base para saber".
    //
    // A diferença não é cosmética. `Segura` e `SemSinal` são 1 e 0 na coluna
    // `zona` do log de viagem, e foi exatamente essa ambiguidade que impediu
    // de decidir, pelo log de 08/10/2026, se o aparelho não tinha radar por
    // perto ou não tinha base para procurar.
    Bancada b;
    // Vetor COM espaço e contagem zero: `data()` de um vetor vazio devolve
    // nullptr e cairia na guarda antiga por acidente, sem exercitar nada.
    // `g_pontos` tem 24000 posições sempre — o que varia é quantas valem.
    std::vector<Ponto> reservada(8);
    b.piloto.define_base(reservada.data(), 0);
    b.dirige(60.0F, 0, 1000);
    EXPECT_EQ(b.piloto.veredito().zona, Zona::SemSinal);
}

TEST(PilotoAlerta, base_vazia_nao_e_confundida_com_via_livre_no_LED) {
    Bancada b;
    std::vector<Ponto> reservada(8);
    b.piloto.define_base(reservada.data(), 0);
    b.dirige(60.0F, 0, 1000);
    EXPECT_EQ(b.led.atual, cores::kAzul);   // azul é "sem sinal", não verde
}

TEST(PilotoAlerta, perder_o_fix_no_meio_apaga_o_alerta) {
    Bancada b;
    b.com_radar(140.0F, 60);
    b.dirige(90.0F, 0, 3000);
    ASSERT_EQ(b.piloto.veredito().zona, Zona::Perigo);
    // Um tunel: para de chegar sentenca.
    for (std::uint32_t t = 3050; t <= 5000; t += 50) { b.piloto.passo(t); }
    EXPECT_EQ(b.piloto.veredito().zona, Zona::SemSinal);
    EXPECT_EQ(b.led.atual, cores::kAzul);
    EXPECT_FALSE(b.buzzer.ligado());
}

// ============================================================ as zonas

TEST(PilotoAlerta, via_livre_fica_verde_e_silenciosa) {
    Bancada b;
    b.com_radar(1000.0F, 60);      // longe demais
    b.dirige(60.0F, 0, 2000);
    EXPECT_EQ(b.piloto.veredito().zona, Zona::Segura);
    EXPECT_EQ(b.led.atual, cores::kVerde);
    EXPECT_FALSE(b.buzzer.ligado());
}

TEST(PilotoAlerta, aproximacao_conforme_fica_amarela_e_silenciosa) {
    Bancada b;
    b.com_radar(120.0F, 60);
    b.dirige(55.0F, 0, 2000);
    EXPECT_EQ(b.piloto.veredito().zona, Zona::AproximacaoConforme);
    EXPECT_EQ(b.led.atual, cores::kAmarelo);
    EXPECT_FALSE(b.buzzer.ligado());
}

TEST(PilotoAlerta, margem_pisca_rosa_e_continua_silenciosa) {
    Bancada b;
    b.com_radar(120.0F, 60);
    b.dirige(63.0F, 0, 2000);
    ASSERT_EQ(b.piloto.veredito().zona, Zona::AproximacaoMargem);
    EXPECT_EQ(b.buzzer.ms_ligado, 0U) << "margem nao soa (RF03.9)";
    // Piscou: em dois segundos a 1 Hz o LED passou por rosa e por apagado.
    bool viu_rosa = false, viu_apagado = false;
    for (std::uint32_t t = 2000; t <= 4000; t += 50) {
        b.uart.emite_rmc(kLat0, kLon0, 63.0F, 0.0F);
        b.piloto.passo(t);
        if (b.led.atual == cores::kRosa) { viu_rosa = true; }
        if (b.led.atual == cores::kApagado) { viu_apagado = true; }
    }
    EXPECT_TRUE(viu_rosa);
    EXPECT_TRUE(viu_apagado);
}

TEST(PilotoAlerta, perigo_pisca_vermelho_e_soa) {
    Bancada b;
    b.com_radar(120.0F, 60);
    b.dirige(90.0F, 0, 3000);
    ASSERT_EQ(b.piloto.veredito().zona, Zona::Perigo);
    EXPECT_GT(b.buzzer.ms_ligado, 0U);
    bool viu_vermelho = false;
    for (std::uint32_t t = 3000; t <= 3500; t += 50) {
        b.uart.emite_rmc(kLat0, kLon0, 90.0F, 0.0F);
        b.piloto.passo(t);
        if (b.led.atual == cores::kVermelho) { viu_vermelho = true; }
    }
    EXPECT_TRUE(viu_vermelho);
}

TEST(PilotoAlerta, semaforo_alterna_amarelo_e_vermelho_sem_apagar) {
    Bancada b;
    b.com_radar(120.0F, kSemLimite);
    b.dirige(60.0F, 0, 2000);
    ASSERT_EQ(b.piloto.veredito().zona, Zona::Semaforo);
    bool amarelo = false, vermelho = false, apagado = false;
    for (std::uint32_t t = 2000; t <= 3500; t += 50) {
        b.uart.emite_rmc(kLat0, kLon0, 60.0F, 0.0F);
        b.piloto.passo(t);
        if (b.led.atual == cores::kAmarelo) { amarelo = true; }
        if (b.led.atual == cores::kVermelho) { vermelho = true; }
        if (b.led.atual == cores::kApagado) { apagado = true; }
    }
    EXPECT_TRUE(amarelo);
    EXPECT_TRUE(vermelho);
    EXPECT_FALSE(apagado) << "semaforo alterna entre cores, nao contra o escuro";
    EXPECT_EQ(b.buzzer.ms_ligado, 0U) << "so a Zona de Perigo soa (RF03.8)";
}

// ====================================================== fiação e detalhes

TEST(PilotoAlerta, o_buzzer_nao_fica_presso_ligado) {
    // A regressao que o CadenciaBuzzer previne: o laco chama define_faixa a
    // cada volta, e se isso reiniciasse a fase o bipe nunca terminaria.
    Bancada b;
    b.com_radar(140.0F, 60);
    unsigned desligado = 0;
    for (std::uint32_t t = 0; t <= 4000; t += 50) {
        if (t % 250 == 0) { b.uart.emite_rmc(kLat0, kLon0, 90.0F, 0.0F); }
        b.piloto.passo(t);
        if (!b.buzzer.ligado()) { ++desligado; }
    }
    EXPECT_GT(desligado, 10U) << "virou tom continuo";
}

TEST(PilotoAlerta, trocar_a_base_esquece_o_alvo) {
    Bancada b;
    b.com_radar(120.0F, 60);
    b.dirige(90.0F, 0, 3000);
    ASSERT_TRUE(b.piloto.veredito().tem_alvo);

    std::vector<Ponto> outra{radar(1000.0F, 60)};
    b.piloto.define_base(outra.data(), outra.size());
    b.dirige(90.0F, 3000, 4000);
    EXPECT_EQ(b.piloto.veredito().zona, Zona::Segura);
}

TEST(PilotoAlerta, a_base_nova_nao_herda_a_severidade_da_antiga) {
    // O alvo em si ja esta protegido: o Zonamento identifica por coordenada
    // desde o R-51. O que `reinicia()` acrescenta e zerar a HISTERESE. A
    // 65 km/h contra limite 60 (V_infra 66) o veredito depende disso: vindo
    // de Perigo o limiar esta rebaixado para 64 e 65 ainda e Perigo; do
    // zero, 65 nao passa de 66 e e Margem. Uma base nova nao pode chegar
    // ja em Perigo por causa da anterior.
    Bancada b;
    b.com_radar(140.0F, 60);
    b.dirige(90.0F, 0, 3000);
    ASSERT_EQ(b.piloto.veredito().zona, Zona::Perigo);

    std::vector<Ponto> outra{radar(140.0F, 60)};
    b.piloto.define_base(outra.data(), outra.size());
    b.dirige(65.0F, 3000, 4500);
    EXPECT_EQ(b.piloto.veredito().zona, Zona::AproximacaoMargem)
        << "a base nova herdou o limiar rebaixado da antiga";
}

TEST(PilotoAlerta, o_monitor_de_taxa_acompanha_o_laco) {
    Bancada b;
    b.com_radar(1000.0F, 60);
    b.dirige(60.0F, 0, 4000);
    EXPECT_EQ(b.gps.monitor().estado(), EstadoTaxa::Nominal);
}

// ================================================= a curva e o filtro de pista
//
// O laco do firmware nao dorme: entre duas sentencas (250 ms a 4 Hz) ele da
// centenas de voltas com a MESMA telemetria. A janela de giro tem doze
// posicoes pensadas para 3 s de fixes; enchida a cada volta, ela guarda doze
// copias de um fix so, a proa parece sempre reta, e numa curva o filtro de
// pista (RF02.4) veta o radar que esta a frente na propria via.

/// Quem percorre um arco de raio `raio_m` virando a direita, partindo da
/// origem rumo norte, depois de andar `s_m` sobre ele.
struct NoArco {
    float norte_m, leste_m, rumo_graus;
};
NoArco no_arco(double raio_m, double s_m) {
    constexpr double kPi = 3.14159265358979323846;
    const double phi = s_m / raio_m;
    return {static_cast<float>(raio_m * std::sin(phi)),
            static_cast<float>(raio_m * (1.0 - std::cos(phi))),
            static_cast<float>(phi * 180.0 / kPi)};
}

float lat_de(float norte_m) { return kLat0 + norte_m / geo::kMetrosPorGrauLat; }
float lon_de(float leste_m) {
    return kLon0 +
           leste_m / (geo::kMetrosPorGrauLat * geo::cosseno_latitude(kLat0));
}

/// Percorre a curva como o firmware: uma RMC a cada 250 ms e o laco dando
/// uma volta a cada 10 ms entre elas. O radar fica SOBRE o arco, `a_frente_m`
/// adiante de onde o carro para, e o carro anda antes o bastante para a
/// janela de giro encher de curva.
Veredito percorre_curva(float kmh, float graus_por_s, float a_frente_m,
                        std::uint8_t limite) {
    constexpr double kPi = 3.14159265358979323846;
    const double v_ms = kmh / 3.6;
    const double raio = v_ms / (graus_por_s * kPi / 180.0);
    const double percurso_m = 100.0;   // ~4 s a 80 km/h, ~9 s a 40 km/h
    const NoArco r = no_arco(raio, percurso_m + a_frente_m);

    Bancada b;
    b.base.push_back(radar(r.norte_m, limite));
    b.base.back().lon = lon_de(r.leste_m);
    b.piloto.define_base(b.base.data(), b.base.size());

    const auto duracao_ms = static_cast<std::uint32_t>(percurso_m / v_ms * 1000.0);
    for (std::uint32_t t = 0; t <= duracao_ms; t += 10) {
        if (t % 250 == 0) {
            const NoArco c = no_arco(raio, v_ms * t / 1000.0);
            b.uart.emite_rmc(lat_de(c.norte_m), lon_de(c.leste_m), kmh,
                             c.rumo_graus);
        }
        b.piloto.passo(t);
    }
    return b.piloto.veredito();
}

TEST(PilotoAlerta, numa_curva_o_radar_a_frente_na_propria_via_nao_e_vetado) {
    // 4 graus/s a 80 km/h e uma curva de ~320 m de raio. O radar 145 m
    // adiante, sobre a via, fica 32 m ao lado da reta que a proa projeta --
    // mais que os 25 m que o filtro tolera ali. So a janela de giro, vendo
    // a curva, desliga o veto. Com a janela cheia de copias ela nao ve.
    const Veredito v = percorre_curva(80.0F, 4.0F, 145.0F, 60);
    EXPECT_TRUE(v.tem_alvo) << "o veto lateral escondeu o radar da curva";
    EXPECT_EQ(v.zona, Zona::Perigo);
}

TEST(PilotoAlerta, curva_suave_de_dois_graus_por_segundo_tambem_abre_o_veto) {
    // A curva que a constante do giro cita como a que desliga o veto. A
    // 40 km/h, 2 graus/s tambem sao ~320 m de raio, e o radar 145 m adiante
    // fica 32 m ao lado da proa. Em 12 fixes a 4 Hz a janela ve 5,5 graus.
    const Veredito v = percorre_curva(40.0F, 2.0F, 145.0F, 40);
    EXPECT_TRUE(v.tem_alvo) << "o veto lateral escondeu o radar da curva";
    EXPECT_EQ(v.alvo.limite, 40);
}

TEST(PilotoAlerta, na_reta_o_mesmo_padrao_de_laco_mantem_o_veto) {
    // O contrapeso: a mesma cadencia de laco, sem curva, e o radar 32 m ao
    // lado da via continua vetado. Sem isto o teste acima passaria com o
    // filtro de pista simplesmente desligado.
    Bancada b;
    b.base.push_back(radar(140.0F, 60));
    b.base.back().lon = lon_de(32.0F);
    b.piloto.define_base(b.base.data(), b.base.size());
    for (std::uint32_t t = 0; t <= 4000; t += 10) {
        if (t % 250 == 0) { b.uart.emite_rmc(kLat0, kLon0, 80.0F, 0.0F); }
        b.piloto.passo(t);
    }
    EXPECT_FALSE(b.piloto.veredito().tem_alvo);
}

// ===================================================== dia e noite

TEST(PilotoAlerta, o_periodo_avanca_mesmo_sem_base_carregada) {
    // Dia ou noite nao depende de haver radares no cartao. Preso ao ramo
    // do alerta, um aparelho com a base ausente ficaria no preset de dia
    // a noite inteira -- e a base ausente e exatamente quando se vai
    // mexer no menu para atualiza-la.
    Bancada b;  // sem com_radar(): base_ == nullptr
    b.dirige(0.0F, 0, 1000);
    EXPECT_NE(b.piloto.periodo(), PeriodoDoDia::Desconhecido);
}

}  // namespace
