#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/Nmea.h"
#include "nucleo/Ponto.h"
#include "nucleo/VelocidadeCruzeiro.h"

namespace coruja {

/// Estado de via apresentado ao motorista.
///
/// A ordem do enum é a **gravidade crescente**, e a precedência do RF03.4
/// depende disso: com vários pontos na janela, vence o de maior valor. Não
/// reordene sem reler aquele requisito.
enum class Zona : std::uint8_t {
    SemSinal = 0,             ///< sem fix — alertas suspensos, LED azul fixo
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

    /// Quantos pontos passaram por TODOS os filtros — raio, sentido, "à
    /// frente" e pista. É o tamanho da disputa que a precedência do RF03.4
    /// resolveu.
    ///
    /// 🔴 **É a contagem ANTES do desempate do RF03.11**, e de propósito.
    /// Pôr aqui o número de depois mostraria `1` justamente nos instantes em
    /// que houve ambiguidade e ela foi resolvida — apagando do log a única
    /// evidência de que o desempate agiu. O campo existe para denunciar
    /// disputa, não para esconder que ela foi arbitrada.
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

/// Raio de alerta dos pontos de limite BAIXO, e a saída correspondente.
///
/// 🔴 **Os 300 m são orçamento de via rápida, e numa marginal viram ruído.**
/// Medido em 28/10/2026, numa via rápida com marginal: numa amostra o
/// radar da pista principal
/// estava a 7,7 m e o da marginal a 171,9 m — ambos na janela, e o da
/// marginal, de limite menor, venceu por gravidade. A 80 km/h, 150 m ainda
/// são 6,7 s de aviso; a 60, são 9,0 s. É aviso de sobra para o pé sair do
/// acelerador, e metade da janela em que a via ao lado consegue se intrometer.
///
/// A janela longa fica para quem corre de verdade: a 120 km/h, 150 m seriam
/// 4,5 s, apertado demais.
constexpr float kRaioAlertaCurtoM = 150.0F;
constexpr float kRaioSaidaCurtoM  = 190.0F;

/// Limite a partir do qual o ponto merece a janela longa.
constexpr std::uint8_t kLimiteJanelaLongaKmh = 100;

/// O raio de entrada deste ponto.
///
/// Semáforo (`limite == 0`) fica com a janela curta, e de propósito: ele é
/// ponto urbano, onde a via ao lado está mais perto ainda.
constexpr float raio_de_alerta(std::uint8_t limite) {
    return limite >= kLimiteJanelaLongaKmh ? kRaioAlertaM : kRaioAlertaCurtoM;
}

/// O raio de saída deste ponto — a histerese do RF03.2, na mesma escala.
constexpr float raio_de_saida(std::uint8_t limite) {
    return limite >= kLimiteJanelaLongaKmh ? kRaioSaidaM : kRaioSaidaCurtoM;
}

/// Afastamento lateral máximo para o ponto contar como "na minha via".
///
/// 🔴 **O número veio de medida, não de palpite.** Em 28/10/2026 refiz a
/// conta das viagens reais contra a trilha percorrida (ponto-a-segmento, e
/// não ponto-a-amostra: a viagem grava a cada 6 s, que a 78 km/h são 130 m
/// entre pontos, e isso sozinho joga dezenas de metros em cima do número).
/// Em 314 radares a até 120 m de alguma trilha há um vale claro entre ~10 e
/// ~15 m: 98 pontos abaixo de 10 m, 14 entre 10 e 15, e 78 entre 15 e 25.
///
/// Dos três alarmes falsos medidos, este limiar mata dois — o radar de 60 a
/// 27,3 m da trilha e o de 40 a 72,8 m. **Não mata o terceiro:** numa delas o
/// 60 intruso fica a 6,6 m da trilha contra 3,4 m do 80 que é meu, e nenhum
/// limiar separa isso. Quem resolve aquele é o desempate por cruzeiro.
constexpr float kDesvioLateralM = 15.0F;

/// Quanto o limiar lateral afrouxa por metro de distância à frente.
///
/// **A perpendicular de longe é um chute.** O aparelho decide ANTES de passar
/// pelo ponto, então não tem a trilha — só pode projetar pela proa de agora,
/// e a 150 m um grau de erro de proa já são 2,6 m de lateral. Em vez de vetar
/// com um número que não se sustenta, o limiar cresce com a distância: perto
/// o veto é afiado, longe ele quase não existe.
///
/// 0,07 é a tangente de 4°, que cobre o erro de proa do NEO-M8N (Doppler,
/// ~0,5°) com folga larga para curva suave. A 150 m o limiar vira 25,5 m — o
/// radar da marginal, a 27,3 m, ainda cai.
constexpr float kFolgaLateralPorMetro = 0.07F;

/// Giro acumulado acima do qual a proa não serve para projetar nada.
///
/// A folga por metro absorve erro de medida, não curva de verdade: numa curva
/// de 2°/s, os 6,7 s que o veículo leva para vencer 150 m valem 13° de giro,
/// e 13° a 150 m são 34 m de lateral — mais que o próprio limiar. Aqui o veto
/// desliga e o alerta volta a ser o de antes. **Na dúvida, abre.**
constexpr float kGiroMaximoGraus = 8.0F;

/// Quantas proas entram na janela do giro. A 4 Hz são 3 s, que é a ordem do
/// tempo de voo até o ponto mais distante que o veto alcança.
///
/// Se a taxa cair, a janela estica e o guarda fica mais severo — mais giro
/// cabe nela, o veto desliga mais vezes, e o alerta volta ao comportamento
/// antigo. A degradação é para o lado seguro.
constexpr std::size_t kAmostrasDeProa = 12;

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

/// Decompõe o vetor veículo→ponto nas direções do rumo e perpendicular a ele.
///
/// `ao_longo_m` positivo é à frente; `lateral_m` positivo é à direita. Devolve
/// `false` — e não escreve nada — quando o rumo não serve, que é o caso em que
/// não existe "perpendicular a quê".
bool decompoe_lateral(const RumoFiltro& rumo, const Telemetria& t,
                      const Ponto& p, float cos_lat, float* ao_longo_m,
                      float* lateral_m);

/// O ponto está na via do veículo, ou na de ao lado? (RF02.4)
///
/// 🔴 **É o filtro que o alarme da via rápida com marginal pediu.** Sem ele,
/// o radar de 60 da lateral entra na janela de quem corre a 80 na principal, vence
/// por gravidade (RF03.4) e toca o buzzer a viagem inteira. Com ele, o ponto
/// lateralmente afastado some antes de disputar.
///
/// **Abre em tudo que for dúvida**, na mesma linha dos outros dois filtros:
/// sem rumo utilizável, com a proa girando, ou com o ponto para trás. O custo
/// de esconder um radar de verdade é maior que o de mostrar um alheio, e é só
/// por haver medida do afastamento que se aceita vetar alguma coisa.
bool ponto_na_minha_via(const RumoFiltro& rumo, const Telemetria& t,
                        const Ponto& p, float cos_lat, bool proa_estavel);

/// Quanto a proa girou na janela recente.
///
/// Guarda as últimas `kAmostrasDeProa` leituras boas e compara a atual com a
/// mais antiga. **Compara as pontas, e não soma os passos**: somar passos
/// acumularia o ruído de cada amostra — meio grau por leitura, doze leituras,
/// e uma reta viraria "curva" de 6°. A diferença entre as pontas carrega o
/// ruído de duas leituras só.
class ProaRecente {
public:
    /// Registra uma proa utilizável. Chamar só quando houver rumo de verdade.
    void anota(float graus);

    /// Esquece a janela. Obrigatório quando o rumo deixa de ser utilizável:
    /// um veículo parado ou manobrando invalida a continuidade.
    void esquece();

    /// Há janela cheia e o giro nela cabe em `kGiroMaximoGraus`?
    /// Janela incompleta responde `false` — ainda não há o que afirmar.
    bool estavel() const;

private:
    float       proas_[kAmostrasDeProa] = {};
    std::size_t quantas_ = 0;   ///< total anotado, satura na capacidade
    std::size_t proxima_ = 0;   ///< onde a próxima entra
};

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

    /// A média que diz em que via o veículo está. Exposta para o teste e
    /// para quem quiser registrá-la; a máquina a alimenta sozinha.
    const VelocidadeCruzeiro& cruzeiro() const { return cruzeiro_; }

    /// Decide qual rumo os filtros usam, e mantém a memória dele.
    ///
    /// Público porque é uma regra própria, com requisito próprio, e merece
    /// teste direto — na mesma linha das outras partes expostas acima. Tem
    /// estado: cada chamada pode atualizar a memória.
    RumoFiltro rumo_para_filtro(const Telemetria& t, float cos_lat);

private:
    /// O mais próximo de uma categoria do RF03.4.
    struct Candidato {
        bool        tem = false;
        std::size_t indice = 0;
        float       distancia = 0.0F;
        void considera(std::size_t i, float d);
    };

    /// O que uma varredura da janela encontrou.
    struct Disputa {
        Candidato    perigo;
        Candidato    margem;
        Candidato    semaforo;
        Candidato    conforme;
        Candidato    mais_proximo;
        std::uint8_t n_candidatos = 0;
        /// Maior limite, entre os candidatos, que o cruzeiro ainda explica.
        /// Zero quando nenhum explica — e aí não há desempate a fazer.
        std::uint8_t limite_da_via = 0;
        /// Menor limite entre os candidatos que têm limite. Zero = nenhum
        /// candidato de velocidade, só semáforo ou nada.
        std::uint8_t menor_limite = 0;
    };

    /// Uma passada pela janela. `limite_minimo` descarta pontos de limite
    /// abaixo dele — é por onde o desempate do RF03.10 entra, numa segunda
    /// passada, sem duplicar nenhum dos filtros.
    Disputa varre(const Telemetria& t, const Ponto* base, std::size_t n,
                  float cos_lat, const RumoFiltro& rumo, bool proa_estavel,
                  std::uint8_t limite_minimo) const;

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

    ProaRecente        proa_;
    VelocidadeCruzeiro cruzeiro_;

    /// Último rumo medido com velocidade suficiente, e onde ele foi medido.
    /// A posição é o que permite saber se o veículo se moveu desde então.
    bool          tem_rumo_lembrado_ = false;
    float         rumo_lembrado_ = 0.0F;
    float         lat_rumo_ = 0.0F;
    float         lon_rumo_ = 0.0F;
};

}  // namespace coruja
