#pragma once
#include <cstdint>

namespace coruja {

/// Constante de tempo da média.
///
/// 🔴 **12 s, e o número veio de uma rampa real.** Comecei com 20 s por achar
/// que "lento é mais seguro". A viagem de 28/10/2026 mostrou o custo: entrando
/// na via rápida o motorista saiu de 48,8 para 80,9 km/h em 30 s, e no
/// instante do alarme a média de 20 s marcava 72,45 — teto de 79,7, que deixa
/// o limite de 80 **0,3 km/h de fora**. O desempate não chegava a tempo
/// justamente na entrada da via, que é quando ele faz falta.
///
/// Com 12 s a mesma rampa dá 75,45, teto 83,0, e o 80 entra com folga de
/// 3 km/h. O teste `a_rampa_de_entrada_na_via_rapida` guarda esse número.
///
/// Lento o bastante ainda: uma arrancada de 4 s de 60 para 100 move a média
/// só 25% do caminho. Uma de 8 s move 40%, e aí promove — é o limite aceito,
/// e está escrito no RF03.11.
constexpr std::uint32_t kTauCruzeiroMs = 12000;

/// Quanto tempo de movimento contínuo antes de o número valer.
constexpr std::uint32_t kAquecimentoCruzeiroMs = 20000;

/// Abaixo disto não há "via" a descrever: é trânsito parado, manobra ou
/// garagem. A média de um carro andando a passo não diz em que pista ele está.
constexpr float kVelocidadeMinimaCruzeiroKmh = 30.0F;

/// Buraco entre amostras que quebra a continuidade. Sem fix por mais que isto,
/// o veículo pode ter mudado de via, e a média de antes passa a mentir.
///
/// 10 s é o mesmo prazo do `kDescarteAlvoMs` do zonamento, e de propósito: lá
/// já está decidido que abaixo disso um viaduto não invalida o que se sabia.
/// Duas respostas diferentes para a mesma pergunta — "a continuidade vale?" —
/// seriam duas regras para manter em dia.
///
/// Tem um efeito colateral útil: o log de viagem grava a cada 6 s, então um
/// arquivo de viagem pode ser **reexecutado** por esta classe sem que a média
/// se reinicie a cada amostra.
constexpr std::uint32_t kBuracoMaximoCruzeiroMs = 10000;

/// Quanto o cruzeiro pode ficar ABAIXO do limite e ainda explicá-lo.
///
/// 10% cobre quem anda a 78 numa via de 80 — que é exatamente o caso medido.
/// Sem a folga, "o maior limite que o cruzeiro explica" devolveria 60 para um
/// motorista a 78 km/h, que é o erro que este desempate existe para evitar.
constexpr float kFolgaDoLimite = 1.10F;

/// A velocidade que descreve a via, e não o instante.
///
/// 🔴 **Existe por causa do caso que a geometria não resolve.** Medido em
/// 28/10/2026, numa via rápida com marginal: o radar de 60 da lateral fica a
/// 6,6 m da trilha percorrida
/// contra 3,4 m do radar de 80 da pista principal. A separação é da ordem do
/// ruído do GPS, e **nenhum** limiar lateral (RF02.4) a encontra. O que separa
/// é outra evidência: quem vem há minutos a 78 km/h está na via de 80, não na
/// marginal de 60.
///
/// **Média exponencial, e não janela de amostras.** Guardar 30 s a 4 Hz
/// custaria 120 posições para produzir um número só; o que se quer daqui é um
/// valor lento, não um histórico. São dois floats e dois carimbos.
class VelocidadeCruzeiro {
public:
    /// Uma leitura de velocidade. Usa o relógio para que o resultado não
    /// dependa da taxa do receptor: a 1 Hz ou a 4 Hz a média tem a mesma
    /// constante de tempo.
    void anota(float velocidade_kmh, std::uint32_t agora_ms);

    /// Larga a média. Obrigatório quando o veículo para, quando o sinal some
    /// por tempo demais, ou ao recarregar a base.
    void esquece();

    /// Houve movimento contínuo o bastante para o número significar algo?
    bool valida() const { return valida_; }

    /// A média. **Só tem sentido com `valida()`**; fora disso é o que sobrou
    /// da última vez.
    float kmh() const { return media_kmh_; }

private:
    float         media_kmh_ = 0.0F;
    std::uint32_t desde_ms_ = 0;    ///< início do trecho contínuo atual
    std::uint32_t ultimo_ms_ = 0;
    bool          tem_amostra_ = false;
    bool          valida_ = false;
};

}  // namespace coruja
