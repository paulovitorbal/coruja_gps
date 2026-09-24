#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/Nmea.h"
#include "nucleo/Ponto.h"

namespace coruja {

/// Estado de via apresentado ao motorista.
///
/// A ordem do enum é a **gravidade crescente**, e a precedência do RF03.4
/// depende disso: com vários pontos na janela, vence o de maior valor. Não
/// reordene sem reler aquele requisito.
enum class Zona : std::uint8_t {
    SemSinal = 0,             ///< sem fix — alertas suspensos, LED apagado
    Segura,                   ///< nenhum ponto válido a ≤ 300 m
    AproximacaoConforme,      ///< `velocidade ≤ limite` — amarelo fixo
    Semaforo,                 ///< `limite == 0`
    AproximacaoMargem,        ///< `limite < velocidade ≤ V_infra` — rosa 1 Hz
    Perigo,                   ///< `velocidade > V_infra` — vermelho 4 Hz
};

/// Cadência do buzzer. **Só a Zona de Perigo soa** (RF03.8): o buzzer tem um
/// único significado — "você está sendo multado se não reduzir" — e a
/// frequência codifica a gravidade. Som em estado sem ação associada tira a
/// autoridade do alerta e vira ruído.
enum class FaixaSonora : std::uint8_t {
    Nenhuma = 0,
    Lenta,    ///< 100 ms a cada 1000 ms
    Rapida,   ///< 100 ms a cada 350 ms
    Pulso,    ///< 50 ms a cada 100 ms — pulso, e **não** tom contínuo
};

const char* descreve(Zona zona);
const char* descreve(FaixaSonora faixa);

struct Veredito {
    Zona        zona = Zona::Segura;
    FaixaSonora faixa = FaixaSonora::Nenhuma;
    bool        tem_alvo = false;
    Ponto       alvo{};              ///< o ponto que determinou a zona
    float       distancia_m = 0.0F;
    /// `V_infra` do alvo, para a tela poder mostrar a margem que resta.
    float       v_infra_kmh = 0.0F;
};

// --------------------------------------------------------------- constantes

constexpr float kRaioAlertaM = 300.0F;   ///< entra em zona de alerta (RF03.2)
constexpr float kRaioSaidaM  = 340.0F;   ///< só sai acima disto — histerese

/// Ângulo máximo entre o rumo do veículo e o azimute até o ponto para ele
/// contar como "à frente" (RF03.1). Sem isto o alerta continuaria ativo
/// depois de passar pelo radar, com o buzzer tocando às costas dele.
constexpr float kAnguloAFrenteGraus = 90.0F;

/// Tolerância do filtro de sentido para pontos unidirecionais (RF02.3).
constexpr float kToleranciaRumoGraus = 30.0F;

/// Abaixo desta velocidade o azimute do NEO-M8N é **ruído aleatório**, e o
/// filtro de rumo tem de **abrir**, não fechar (RF02.3). Descartar por um rumo
/// que é ruído perderia pontos reais.
constexpr float kVelocidadeMinimaRumoKmh = 5.0F;

/// Histerese nas fronteiras de velocidade: a faixa sobe ao cruzar o limiar e
/// só desce 2 km/h abaixo dele (RF03.7). Sem isso, velocidade oscilando sobre
/// uma fronteira faria o padrão sonoro tremular.
constexpr float kHistereseVelKmh = 2.0F;

/// Sem fix, o alvo é descartado após 10 s (§4.1). Até lá ele é preservado,
/// para que um viaduto não custe o alerta; depois disso a posição está velha
/// demais para se alertar sobre ela.
constexpr std::uint32_t kDescarteAlvoMs = 10000;

// ------------------------------------------------------- partes testáveis
//
// Expostas porque cada uma responde por um requisito distinto e merece teste
// direto, sem ter de montar uma base e uma máquina inteira para exercitá-las.

/// Índice do primeiro ponto com latitude ≥ `lat`, por busca binária — o
/// primeiro estágio do RF02.1. A base **tem** de estar ordenada por latitude;
/// o conversor garante isso, e é o que torna a varredura O(log n + k) em vez
/// de percorrer os 18 mil pontos a 4 Hz.
std::size_t primeiro_com_lat_ge(const Ponto* base, std::size_t n, float lat);

/// O ponto está à frente do veículo? (RF03.1)
///
/// Distância ≤ 300 m não basta: sem este teste o alerta continuaria ativo
/// depois de passar pelo radar. Compara o azimute veículo→ponto com o rumo.
/// **Abre** (devolve `true`) abaixo de `kVelocidadeMinimaRumoKmh` ou sem rumo
/// válido, porque aí o azimute do receptor é ruído.
bool ponto_a_frente(const Telemetria& t, const Ponto& p, float cos_lat);

/// O sentido do ponto é compatível com o rumo do veículo? (RF02.3)
///
/// Omnidirecional nunca descarta. Unidirecional exige ≤ 30°. Bidirecional
/// vale no rumo indicado **e no oposto**, o que se resolve dobrando a
/// diferença para a faixa 0–90°. Como acima, abre em baixa velocidade.
bool sentido_compativel(const Telemetria& t, const Ponto& p);

/// A máquina de estados de zona do RF03.
///
/// **Não toca em hardware e não lê relógio.** Recebe telemetria, a base e o
/// instante; devolve um veredito. Quem acende LED, toca buzzer ou desenha
/// barra é outro — é o que permite exercitar a lógica inteira no host, e é o
/// mesmo código que roda no Pico (ADR 0001).
///
/// ## Como o alvo é escolhido
///
/// Não é "o ponto mais próximo". O RF03.4 rastreia **quatro candidatos**, o
/// mais próximo de cada categoria, porque o estado é por ponto e não global:
/// dois radares na janela com limites diferentes — um de 60 a 250 m e um de
/// 80 a 100 m, com o veículo a 70 — produzem ao mesmo tempo um candidato de
/// Perigo (o mais distante) e um de conforme (o mais próximo). Usar o mais
/// próximo como estado do sistema esconderia a multa em curso.
class MaquinaZona {
public:
    /// Avalia com um fix válido.
    Veredito avalia(const Telemetria& t, const Ponto* base, std::size_t n,
                    std::uint32_t agora_ms);

    /// Avalia **sem** fix. Alertas ficam suspensos de imediato; o alvo
    /// sobrevive por `kDescarteAlvoMs` para o caso de o sinal voltar logo.
    Veredito sem_fix(std::uint32_t agora_ms);

    /// Esquece o alvo e as histereses. Obrigatório após recarregar a base —
    /// o alvo é guardado por índice, e os índices mudam.
    void reinicia();

private:
    bool e_perigo(float velocidade_kmh, std::uint8_t limite) const;
    FaixaSonora faixa_sonora(float velocidade_kmh, float v_infra);

    Zona          zona_ = Zona::Segura;
    FaixaSonora   faixa_ = FaixaSonora::Nenhuma;
    bool          tem_alvo_ = false;
    std::size_t   indice_alvo_ = 0;
    float         lat_alvo_ = 0.0F;  ///< confirma a identidade do alvo
    float         lon_alvo_ = 0.0F;  ///< se a base for recarregada
    std::uint32_t ultimo_fix_ms_ = 0;
    bool          houve_fix_ = false;
};

}  // namespace coruja
