#pragma once
#include <cstdint>

namespace coruja {

/// Quanto tempo o texto fica parado em cada ponta, antes de rolar e depois
/// de chegar ao fim.
///
/// **As pausas são o que torna o texto legível.** Rolagem sem pausa obriga
/// o olho a perseguir a primeira palavra; parado no início, a frase curta
/// pode ser lida de uma vez, e o movimento passa a ser o complemento para o
/// resto.
constexpr std::uint32_t kPausaRolagemMs = 1500;

/// **Rola de um CARACTERE por vez, não de um pixel.**
///
/// Não é escolha estética — é orçamento de SPI. Cada passo redesenha a
/// linha inteira de texto (320 × 20 px = 12.800 bytes), porque ao deslocar
/// o texto todo pixel da linha muda. Medido:
///
/// | passo | taxa | SPI a 16 MHz |
/// | :--- | ---: | ---: |
/// | 1 px a cada 20 ms | 50/s | **320 ms/s — 32%** |
/// | 12 px a cada 250 ms | 4/s | 26 ms/s — 3% |
///
/// Os 32% seriam inviáveis por um motivo específico: a transferência é
/// bloqueante, e a decodificação do encoder precisa de amostragem a 1 ms
/// contra um pior caso medido de 5,75 ms no estado `(0,0)` (R-36). Um
/// bloqueio de 6,4 ms come essa folga inteira e o giro passa a perder
/// detente.
///
/// Rolagem por caractere ainda é o que letreiros de LED fazem há décadas, e
/// se lê bem.
constexpr int kPassoRolagemPx = 12;
constexpr std::uint32_t kMsPorPassoRolagem = 250;

/// Onde começar a desenhar um texto, e se ele está em movimento.
struct Rolagem {
    /// Coluna inicial. **Pode ser negativa**, e é o painel que recorta.
    int  x = 0;
    /// Se verdadeiro, quem desenha tem de redesenhar enquanto durar: o
    /// valor muda com o tempo, e uma tela que só redesenha ao mudar de
    /// conteúdo congelaria a frase no meio.
    bool rolando = false;
};

/// Decide o deslocamento horizontal de um texto numa largura disponível.
///
/// Cabe: centraliza e não rola — é o caso comum, e centralizado a frase
/// fica equilibrada com o número, que também é centralizado.
///
/// Não cabe: começa alinhado à esquerda, espera, rola até o fim do texto
/// aparecer, espera de novo, e repete. **Não rola em anel contínuo** de
/// propósito: o anel emenda o fim da frase no começo dela, e por um
/// instante se lê uma frase que não existe.
Rolagem rolagem(int largura_texto, int largura_disponivel,
                std::uint32_t agora_ms);

}  // namespace coruja
