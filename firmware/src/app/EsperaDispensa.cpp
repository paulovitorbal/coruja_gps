#include "app/EsperaDispensa.h"

namespace coruja {

void EsperaDispensa::ate_dispensar(Batimento& batimento) {
    // **Esvazia a fila ANTES de esperar.** Quem chega aqui chegou por um
    // clique, e enquanto o OTA rodava — dezenas de segundos, com o laço
    // principal parado — os eventos continuaram se acumulando. Um clique
    // impaciente dispensaria a tela antes de ela chegar a ser lida, e a
    // falha pareceria não ter sido mostrada.
    while (encoder_.proximo_evento() != EventoEncoder::Nenhum) {
    }

    // Pinta primeiro, pergunta depois: quem chamou acabou de mudar o estado
    // e mais ninguém vai pintar. Sair sem ter pintado deixaria no painel o
    // quadro anterior.
    for (;;) {
        batimento.mantem();
        if (encoder_.proximo_evento() != EventoEncoder::Nenhum) {
            return;
        }
        pausa_.espera_ms(kPassoMs);
    }
}

}  // namespace coruja
