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

    /// O candidato **mais próximo**, que em geral NÃO é o `alvo`.
    ///
    /// O alvo vence por **gravidade** (RF03.4), não por distância: dois
    /// radares na janela com limites diferentes põem em Perigo o mais
    /// distante, e é ele que vira alvo. Para a tela isso está certo — a multa
    /// em curso é o que importa.
    ///
    /// **Mas quem registra infração consumada precisa do outro.** "Passei a
    /// menos de 50 m deste radar acima do V_infra dele" é pergunta por ponto,
    /// e respondê-la pelo alvo atribuiria a passagem ao radar errado. Sai de
    /// graça: a máquina já computa os quatro candidatos.
    bool        tem_mais_proximo = false;
    Ponto       mais_proximo{};
    float       dist_mais_proximo_m = 0.0F;

    /// Quantos pontos passaram por TODOS os filtros — raio, sentido e "à
    /// frente". É o tamanho da disputa que a precedência do RF03.4 resolveu.
    ///
    /// Existe para o log de viagem poder dizer se havia ambiguidade. Sem ele,
    /// ambiguidade só se percebe quando o alvo e o mais próximo têm limites
    /// DIFERENTES — dois radares de 60 em pistas distintas passariam por um
    /// caso simples, e não são.
    ///
    /// Satura em 255: o número existe para dizer "um, dois, ou muitos", e
    /// nenhuma decisão depende de distinguir 260 de 255.
    std::uint8_t n_candidatos = 0;
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
/// rumo medido não serve para filtrar (RF02.3).
constexpr float kVelocidadeMinimaRumoKmh = 5.0F;

/// Quanto o veículo pode se deslocar antes de o rumo lembrado perder validade.
///
/// **Carro parado não gira.** É isso que torna o rumo de instantes antes ainda
/// verdadeiro quando o receptor para de informá-lo — e é também o que limita a
/// validade: se o veículo ANDOU sem produzir rumo novo, ele pode ter mudado de
/// direção, e a certeza acaba.
///
/// 25 m cobre o avanço de uma fila de semáforo sem cobrir uma manobra.
constexpr float kRaioRumoCongeladoM = 25.0F;

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

/// O rumo que os filtros devem usar, que nem sempre é o medido agora.
///
/// Separado da `Telemetria` de propósito: o rumo de filtragem pode vir da
/// **memória**, quando o veículo está parado e o receptor deixou de informá-lo.
/// Misturar as duas coisas na mesma estrutura faria o código mentir sobre a
/// procedência do número.
struct RumoFiltro {
    bool  utilizavel = false;   ///< falso = filtro ABRE, como antes
    float graus = 0.0F;
};

/// O ponto está à frente do veículo? (RF03.1)
///
/// Distância ≤ 300 m não basta: sem este teste o alerta continuaria ativo
/// depois de passar pelo radar. Compara o azimute veículo→ponto com o rumo.
/// **Abre** (devolve `true`) quando o rumo não é utilizável — aí não há por
/// onde decidir, e abrir é a falha segura.
bool ponto_a_frente(const RumoFiltro& rumo, const Telemetria& t,
                    const Ponto& p, float cos_lat);

/// O sentido do ponto é compatível com o rumo do veículo? (RF02.3)
///
/// Omnidirecional nunca descarta. Unidirecional exige ≤ 30°. Bidirecional
/// vale no rumo indicado **e no oposto**, o que se resolve dobrando a
/// diferença para a faixa 0–90°. Como acima, abre sem rumo utilizável.
bool sentido_compativel(const RumoFiltro& rumo, const Ponto& p);

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

    /// Decide qual rumo os filtros usam, e mantém a memória dele.
    ///
    /// Público porque é uma regra própria, com requisito próprio, e merece
    /// teste direto — na mesma linha das outras partes expostas acima. Tem
    /// estado: cada chamada pode atualizar a memória.
    RumoFiltro rumo_para_filtro(const Telemetria& t, float cos_lat);

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

    /// Último rumo medido com velocidade suficiente, e onde ele foi medido.
    /// A posição é o que permite saber se o veículo se moveu desde então.
    bool          tem_rumo_lembrado_ = false;
    float         rumo_lembrado_ = 0.0F;
    float         lat_rumo_ = 0.0F;
    float         lon_rumo_ = 0.0F;
};

}  // namespace coruja
