#pragma once

#include "encoder/EventoEncoder.h"
#include "led/Cor.h"

namespace coruja {

/// Modo de teste de bancada: prova a cadeia encoder -> LED sem precisar de
/// GPS, cartão nem display.
///
///   girar à esquerda  -> vermelho
///   girar à direita   -> azul
///   clicar            -> apaga
///
/// Isto **não** é o comportamento de produção. O encoder em operação ajusta o
/// brilho ao girar e comanda a atualização OTA ao clicar (`requirements.md`
/// matriz de IHM), e o LED carrega o estado de via. Este modo existe para
/// validar a fiação e a decodificação — e, de passagem, serve ao R-05: é com
/// ele que se compara vermelho e azul lado a lado.
class ModoTesteEncoder {
public:
    /// Aplica um evento e devolve a cor resultante. Evento `Nenhum` mantém a
    /// cor: quem chama pode invocar a cada ciclo sem piscar o LED.
    Cor aplica(EventoEncoder evento);

    Cor cor() const { return cor_; }

private:
    Cor cor_ = cores::kApagado;
};

}  // namespace coruja
