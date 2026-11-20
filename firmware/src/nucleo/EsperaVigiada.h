#pragma once
#include "nucleo/CaoDeGuarda.h"

namespace coruja {

/// Como uma espera vigiada terminou.
enum class FimDaEspera { Pronto, Expirou };

/// O laço de espera de rede, com a refeição no ÚNICO lugar em que ela é
/// honesta: DEPOIS de `passo()` voltar.
///
/// `passo` é a chamada que pode travar por dentro do SDK — `cyw43_arch_poll`,
/// lwIP, mbedTLS. Se ela travar, a refeição seguinte nunca acontece e o cão
/// morde, que é o que ele existe para fazer. Se o laço girar por LÓGICA
/// (nada chega, nada anda), quem o encerra é `expirou`, que é o prazo próprio
/// da espera — limitado por construção. O que a regra "alimentar por
/// progresso" proíbe é alimentar num laço que não passa por nenhuma chamada
/// capaz de travar, ou que não tem prazo próprio; aqui há as duas coisas.
/// Ver `CaoDeGuarda.h`.
///
/// **Alimenta também antes de devolver `Expirou`**: quem chama vai registrar a
/// desistência no log, e isso força uma descarga no cartão que pode levar
/// segundos. Sem a refeição, a margem para registrar seria o que sobrou do
/// prazo do cão (1,5 s), e um cartão lento não cabe nela.
///
/// É modelo (template) e mora no núcleo para que a regra rode no host contra
/// um `passo` que "trava". `cao` nulo é "ninguém vigia".
template <class Pronto, class Expirou, class Passo>
FimDaEspera espera_vigiada(CaoDeGuarda* cao, Pronto pronto, Expirou expirou,
                           Passo passo) {
    while (!pronto()) {
        if (expirou()) {
            if (cao != nullptr) { cao->alimenta(); }
            return FimDaEspera::Expirou;
        }
        passo();
        if (cao != nullptr) { cao->alimenta(); }
    }
    return FimDaEspera::Pronto;
}

}  // namespace coruja
