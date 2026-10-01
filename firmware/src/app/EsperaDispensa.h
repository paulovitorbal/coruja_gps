#pragma once
#include <cstdint>

#include "encoder/Encoder.h"
#include "nucleo/Pausa.h"

namespace coruja {

/// Alguém que precisa continuar pintando enquanto outra coisa espera.
///
/// Existe por causa do que pisca. O LED é função do TEMPO, não de evento, e
/// durante o OTA o laço principal não roda — se ninguém chamar nada, o LED
/// congela no meio de um piscar e a tela parada vira indistinguível de uma
/// tela travada. Que é exatamente o que a tela de atualização existe para
/// desmentir.
class Batimento {
public:
    virtual ~Batimento() = default;
    virtual void mantem() = 0;
};

/// Segura uma tela até o usuário dispensá-la no encoder.
///
/// **Para resultado que precisa ser LIDO, não vislumbrado.** Uma espera fixa
/// serve para "ATUALIZADA", que é uma palavra só; não serve para uma falha,
/// em que o usuário precisa entender o motivo antes de decidir o que tentar.
/// Dois segundos e meio é pouco para ler e decidir, e quem perdeu a frase não
/// tem como pedir de novo.
///
/// Qualquer evento dispensa — clique ou giro. Quem acabou de ver uma falha
/// mexe no que tiver na mão, e exigir justamente o clique faria o giro
/// parecer que não funcionou.
class EsperaDispensa {
public:
    EsperaDispensa(Encoder& encoder, Pausa& pausa)
        : encoder_(encoder), pausa_(pausa) {}

    void ate_dispensar(Batimento& batimento);

private:
    /// Curto o bastante para não atropelar o piscar. O LED da falha pisca a
    /// 2 Hz, e meio ciclo são 250 ms; passos maiores pulariam estados e o
    /// piscar sairia errático.
    static constexpr std::uint32_t kPassoMs = 50;

    Encoder& encoder_;
    Pausa&   pausa_;
};

}  // namespace coruja
