#pragma once
#include <cstdint>

namespace coruja {

/// Uma palavra de 32 bits que sobrevive a um reset por watchdog.
///
/// No Pico é `watchdog_hw->scratch[0]`. É interface pelo mesmo motivo de
/// `CaoDeGuarda`: a REGRA (o que contar, quando zerar, o que fazer com lixo)
/// é o que precisa de teste, e o registrador não roda no host.
class ArmazemDePalavra {
public:
    virtual ~ArmazemDePalavra() = default;
    virtual std::uint32_t le() const = 0;
    virtual void grava(std::uint32_t valor) = 0;
};

/// O que fazer com o cão a cada boot: quebra o ciclo de reinícios.
///
/// **Existe porque o único efeito de uma mordida era uma linha de log.** Um
/// cartão que segura o MISO ocupado (2000 ms × 3 tentativas por comando) passa
/// pelo arranque, que roda sem cão, e faz o cão morder no primeiro acesso do
/// laço; o aparelho reinicia, o cartão continua ocupado, e o ciclo se repete
/// para sempre — sem diagnóstico, porque a linha de log também depende do
/// cartão. Aparelho que reinicia em laço está pior que aparelho que trava.
///
/// A regra:
///  - a cada boot causado pelo cão, o contador sobe; qualquer outro boot
///    (religamento, gravação por `.uf2`) o zera;
///  - com `kMordidasSeguidasMax` mordidas seguidas, o boot NÃO arma o cão e
///    volta a se comportar como antes do cão existir: pode travar mudo, mas
///    não entra em laço. Esse boot desarmado não morde, então o próximo boot
///    (a próxima ignição) conta como religamento e rearma;
///  - rodar `kSaudavelMs` com o cão armado, sem mordida, zera o contador.
///
/// Pura: nada aqui conhece o registrador nem o relógio de verdade.
class PoliticaDoCao {
public:
    /// Três: a primeira mordida pode ser acaso (um cartão que soluçou), a
    /// segunda é suspeita, e na terceira seguida a hipótese de acaso acabou.
    static constexpr unsigned kMordidasSeguidasMax = 3;

    /// Dez minutos de laço sem mordida.
    ///
    /// O cartão é tocado pelo diário a cada minuto e descarregado pela `vigia`
    /// a cada cinco; um cartão que segura o barramento morde dentro da
    /// primeira dessas cadências. Dez minutos são o DOBRO da mais lenta, então
    /// um ciclo de mordidas nunca é confundido com saúde. E é curto o bastante
    /// para que uma mordida isolada, dias depois, comece de novo do um.
    static constexpr std::uint32_t kSaudavelMs = 10u * 60u * 1000u;

    struct Decisao {
        bool     arma = true;
        unsigned mordidas_seguidas = 0;
    };

    explicit PoliticaDoCao(ArmazemDePalavra& armazem) : armazem_(armazem) {}

    /// Chamada UMA vez, no começo do boot, com a resposta de
    /// `CaoDeGuarda::reiniciou_por_travamento()`. Persiste o contador novo.
    Decisao decide_no_boot(bool veio_de_travamento);

    /// Chamada a cada volta do laço principal. Zera o contador quando o
    /// aparelho roda saudável por `kSaudavelMs` com o cão armado.
    void passo(std::uint32_t agora_ms);

    /// Marca (parte alta) que distingue contagem de lixo no registrador: o
    /// valor de um religamento a frio é indefinido.
    static constexpr std::uint32_t kMarca = 0xC0A70000u;

private:
    static constexpr std::uint32_t kMascaraMarca = 0xFFFF0000u;
    static constexpr std::uint32_t kMascaraContagem = 0x0000FFFFu;

    unsigned le_contagem() const;
    void grava_contagem(unsigned n);

    ArmazemDePalavra& armazem_;
    bool          armado_ = false;
    bool          zerou_ = false;
    bool          viu_o_primeiro_passo_ = false;
    std::uint32_t inicio_ms_ = 0;
};

}  // namespace coruja
