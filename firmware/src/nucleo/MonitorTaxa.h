#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Estado da taxa de fixes, conforme a tabela do RF01.5.
enum class EstadoTaxa : std::uint8_t {
    Aquecendo = 0,  ///< janela ainda não completou: cedo demais para julgar
    Nominal,        ///< ≥ 3,5 Hz
    Degradado,      ///< 3,0 a 3,5 Hz — opera normalmente, registra o evento
    Falha,          ///< < 3,0 Hz sustentado por 5 s
};

const char* descreve(EstadoTaxa estado);

/// Janela deslizante do RF01.5, em milissegundos.
constexpr std::uint32_t kJanelaMs = 3000;
/// Abaixo do piso por este tempo contínuo, é falha — não um vale passageiro.
constexpr std::uint32_t kSustentacaoFalhaMs = 5000;
constexpr float kTaxaNominalHz = 3.5F;
constexpr float kTaxaPisoHz = 3.0F;

/// Quantos instantes cabem na janela. A 4 Hz bastam 12; 32 dá folga de quase
/// 3× para uma rajada. Cheia, o mais antigo cai — o que **subestimaria** a
/// taxa, mas só a partir de 10,7 Hz, muito acima de qualquer classificação
/// que nos interesse: lá em cima o veredito é `Nominal` de qualquer forma.
constexpr std::size_t kCapacidadeJanela = 32;

/// Mede a taxa efetiva de fixes válidos e classifica segundo o RF01.5.
///
/// **Este monitor é, antes de tudo, a verificação de que o `CFG-RATE` do
/// RF01.2 pegou.** Taxa baixa quase nunca é o receptor com dificuldade: o
/// NEO-M8N entrega a taxa configurada enquanto tiver fix, ou perde o fix. A
/// causa provável é configuração que não foi aplicada, UART saturada ou
/// checksum falhando — e é por isso que os checksums inválidos são contados
/// **à parte**. Sem essa separação, "o GPS não está enviando" e "estamos
/// falhando em interpretar" produzem o mesmo sintoma e pedem correções
/// opostas.
///
/// Sem hardware e sem relógio próprio: o instante entra por parâmetro, como
/// no `AntiRepique` e no `Zonamento`.
class MonitorTaxa {
public:
    /// Uma sentença RMC válida **com fix** chegou.
    void registra_fix(std::uint32_t agora_ms);

    /// Uma sentença chegou com checksum inválido. Não entra na taxa: ela
    /// mede o que foi aproveitado, não o que passou no fio.
    void registra_checksum_invalido();

    /// Recalcula e classifica.
    ///
    /// **Tem de ser chamada com regularidade**, não só quando chega um fix. A
    /// sustentação de 5 s do RF01.5 é contada entre chamadas: o que se sabe
    /// num instante é a taxa dos últimos 3 s, e "sustentado" só se estabelece
    /// observando. Chamar uma vez ao fim de um minuto ruim reporta
    /// `Degradado`, não `Falha` — corretamente, porque daquela única
    /// observação não se conclui sustentação.
    ///
    /// Chamá-la também quando **não** chega fix é o que detecta o silêncio:
    /// é a janela esvaziando que faz a taxa cair.
    EstadoTaxa avalia(std::uint32_t agora_ms);

    EstadoTaxa estado() const { return estado_; }
    float taxa_hz() const { return taxa_hz_; }
    std::uint32_t checksums_invalidos() const { return checksums_invalidos_; }

    /// Entrou em falha e a reconfiguração UBX ainda não foi tentada **nesta
    /// falha**. O RF01.5 manda reenviar a sequência do RF01.2 uma vez antes
    /// de escalar para o RF07 — uma, não em laço.
    bool precisa_reconfigurar() const { return precisa_reconfigurar_; }
    void reconfiguracao_enviada() { precisa_reconfigurar_ = false; }

    void reinicia();

private:
    void descarta_antigos(std::uint32_t agora_ms);

    std::uint32_t instantes_[kCapacidadeJanela] = {};
    std::size_t   inicio_ = 0;   ///< índice do mais antigo
    std::size_t   quantos_ = 0;
    std::uint32_t primeiro_ms_ = 0;
    bool          houve_algum_ = false;
    std::uint32_t abaixo_desde_ms_ = 0;
    bool          abaixo_ = false;
    std::uint32_t checksums_invalidos_ = 0;
    EstadoTaxa    estado_ = EstadoTaxa::Aquecendo;
    float         taxa_hz_ = 0.0F;
    bool          precisa_reconfigurar_ = false;
};

}  // namespace coruja
