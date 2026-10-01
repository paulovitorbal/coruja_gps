#pragma once
#include <cstdint>

#include "app/EsperaDispensa.h"
#include "display/TelaOta.h"
#include "display/Visor.h"
#include "led/LedRgb.h"
#include "led/PadraoLedOta.h"
#include "nucleo/Pausa.h"
#include "rede/ObservadorOta.h"

namespace coruja {

/// Mostra a atualização na tela e no LED, de dentro do OTA.
///
/// **Existe porque o `executa()` é bloqueante.** Ele pode levar dezenas de
/// segundos e o laço principal não roda nesse tempo — sem alguém desenhando
/// de dentro, a tela congelaria no último quadro e o usuário não saberia se
/// o aparelho trabalha ou travou.
///
/// É a peça que **junta**: não decide nada sobre rede nem sobre desenho. O
/// `AtualizadorOta` diz o que está acontecendo, a `TelaOta` sabe desenhar, o
/// `PadraoLedOta` sabe a cor, e isto liga os três.
///
/// **O LED pisca sozinho, e por isso o relógio entra aqui.** O piscar é
/// função do tempo, e durante o OTA ninguém mais está chamando nada: se a
/// cor só fosse recalculada nas mudanças de fase, o LED ficaria parado
/// durante todo um download de 214 KB — que é exatamente a fase mais longa
/// e a que mais precisa mostrar que algo acontece.
class OtaNaTela final : public ObservadorOta, public Batimento {
public:
    OtaNaTela(Visor& visor, LedRgb& led, Pausa& relogio)
        : visor_(visor), led_(led), relogio_(relogio) {}

    void fase(FaseOta fase, unsigned tentativa) override;
    void progresso(std::size_t recebidos, std::size_t total) override;

    /// Falhou, e agora se sabe por quê.
    ///
    /// Separado do `fase(Falhou)` porque chega depois: o orquestrador avisa
    /// que falhou de dentro, antes de haver um `ResultadoOta`. Quem tem o
    /// resultado na mão é a composição, e é ela quem completa a tela.
    ///
    /// `motivo` precisa sobreviver à chamada — na prática é literal, vindo
    /// do `descreve_curto()`.
    void falhou(const char* motivo);

    /// Redesenha e atualiza o LED sem que nada tenha mudado.
    ///
    /// Para quem chama entre as notificações — o piscar precisa disso, e as
    /// fases de rede podem passar segundos sem nenhum aviso.
    void mantem() override;

    const EstadoOta& estado() const { return estado_; }

private:
    void pinta();

    Visor&        visor_;
    LedRgb&       led_;
    Pausa&        relogio_;
    TelaOta       tela_;
    PadraoLedOta  padrao_;
    EstadoOta     estado_;
};

}  // namespace coruja
