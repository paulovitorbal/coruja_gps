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
    // ⚠️ **A unica espera do firmware que nao tem fim proprio: quem a
    // encerra e o usuario.** Por isso o cao e alimentado aqui a cada volta,
    // e isso nao e enfeite: a refeicao vem DEPOIS do `mantem()`, que e uma
    // transacao de verdade com o painel. Se o SPI do display travar, a
    // volta nao fecha e o cao morde.
    //
    // Nao alimentar aqui reiniciaria o aparelho 16,5 s depois de ele mostrar
    // a razao de uma falha -- destruindo o unico diagnostico que o usuario
    // recebe, e justamente quando ele esta lendo.
    for (;;) {
        batimento.mantem();
        if (cao_ != nullptr) { cao_->alimenta(); }
        if (encoder_.proximo_evento() != EventoEncoder::Nenhum) {
            return;
        }
        pausa_.espera_ms(kPassoMs);
    }
}

}  // namespace coruja
