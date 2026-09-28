#pragma once
#include <cstdint>

namespace coruja {

/// Cor em RGB565, que é o formato que o painel consome.
using Cor565 = std::uint16_t;

/// Paleta do §4.1. As três regras que a produziram são consequência de o
/// brilho ser PWM do backlight, que multiplica a luminância de tudo pelo
/// mesmo fator:
///
/// 1. **Distinguir por matiz, nunca por luminância** — diferenças de valor
///    são exatamente o que o PWM destrói.
/// 2. **Nunca só por saturação** — um "rosa claro" `#FF8080` tem matiz 0, o
///    mesmo do vermelho; a saturação é a primeira coisa a colapsar no escuro.
/// 3. **O número fica sempre branco** — é o que precisa ser lido, e branco dá
///    21:1 de contraste sobre preto contra 5,3:1 do vermelho.
namespace paleta {
constexpr Cor565 kFundo      = 0x0000;
constexpr Cor565 kTexto      = 0xFFFF;   ///< número, denominador, texto
constexpr Cor565 kDegradado  = 0xC618;   ///< sem base, sem sinal, `- -`
constexpr Cor565 kMoldura    = 0x4208;   ///< fio e trilho vazio da barra
constexpr Cor565 kBarraAmbar = 0xFD80;   ///< conforme e semáforo — matiz 42°
constexpr Cor565 kBarraRosa  = 0xFA18;   ///< margem — matiz 318°
constexpr Cor565 kBarraPerigo = 0xF800;  ///< perigo — matiz 0°
}  // namespace paleta

/// Geometria do §4.1. A soma fecha em 240 e há `static_assert` adiante.
namespace tela {
constexpr int kLargura = 320;
constexpr int kAltura = 240;
constexpr int kMoldura = 2;
constexpr int kFaixaSuperior = 26;
constexpr int kAreaNumero = 166;
constexpr int kFaixaInferior = 44;

constexpr int kYFaixaSuperior = kMoldura;
constexpr int kYAreaNumero = kYFaixaSuperior + kFaixaSuperior;
constexpr int kYFaixaInferior = kYAreaNumero + kAreaNumero;

static_assert(kMoldura + kFaixaSuperior + kAreaNumero + kFaixaInferior +
                      kMoldura == kAltura,
              "as faixas do §4.1 tem de fechar em 240 px");
}  // namespace tela

/// Onde o `x` do texto cai.
///
/// Explícito porque era implícito no tipo de fonte, e isso é armadilha: o
/// número usava `x` como centro e o texto como borda esquerda, sem nada no
/// código dizendo isso. Quem escrevesse uma terceira chamada teria 50% de
/// chance de acertar.
enum class Alinhamento : std::uint8_t { Esquerda, Centro };

enum class Fonte : std::uint8_t {
    Numero,   ///< 56×94, para `velocidade/limite`
    Texto,    ///< 12×20, para as faixas
};

/// Os ícones do §4.1.
///
/// São **dois** sprites em flash e três ícones: o `TYPE=2` compõe semáforo
/// com radar em vez de ser um desenho próprio.
///
/// **Radar móvel não se distingue do fixo** (decidido em 2026-09-28). O §4.1
/// pedia um carro vazado para dizer "este ponto pode não estar aqui hoje",
/// mas a ação do motorista é a mesma nos dois casos — o RF03.5 já manda
/// zonear o móvel igual ao fixo. Distinção que não muda decisão é ruído no
/// instante em que menos se pode gastar atenção.
enum class Icone : std::uint8_t {
    Nenhum,
    Radar,              ///< 🏎 — fixo e móvel, sem distinção
    Semaforo,           ///< 🚦
    SemaforoComRadar,   ///< 🚦 + 🏎
};

/// O painel, visto por quem desenha.
///
/// Não há `framebuffer` nesta interface, e é decisão e não omissão: o §4.1
/// mostra que o layout **não muda entre estados** e que nada pisca, então o
/// redesenho é por região. Um framebuffer de 320×240 RGB565 custaria 150 KB
/// dos 182 KiB livres medidos — mais que a reserva inteira do RNF07 — para
/// evitar um trabalho que não existe.
class Visor {
public:
    virtual ~Visor() = default;

    virtual void retangulo(int x, int y, int largura, int altura,
                           Cor565 cor) = 0;
    virtual void texto(int x, int y, const char* texto, Fonte fonte,
                       Cor565 cor, Alinhamento alinhamento) = 0;
    virtual void icone(int x, int y, Icone icone) = 0;

    /// Empurra o que foi desenhado para o painel.
    virtual void apresenta() = 0;
};

}  // namespace coruja
