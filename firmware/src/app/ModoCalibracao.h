#pragma once
#include <cstdint>

#include "app/ItemCalibracao.h"
#include "encoder/EventoEncoder.h"
#include "led/Cor.h"

namespace coruja {

/// Modo de calibração do LED RGB — a metade do R-05 que sobrou.
///
/// Os resistores foram medidos em 2026-09-19 (330 Ω vermelho, 470 Ω verde,
/// 150 Ω azul). O que falta são as **razões de PWM** que produzem o âmbar da
/// Zona de Semáforo e o rosa da faixa de margem, e isso **não se mede com
/// multímetro**: depende de razão percebida, e o olho não é linear.
///
///     girar   -> ajusta o canal variável do item atual
///     clicar  -> avança para o próximo item, circularmente
///
/// Nas cores compostas o **vermelho fica fixo em 100%** e o encoder ajusta o
/// outro canal. Não é simplificação arbitrária: é a forma como
/// `led/Calibracao.h` guarda as cores — `kAmbar` e `kRosa` têm o canal
/// vermelho em 255 e variam só o secundário.
///
/// **O rosa é o item crítico.** Ele precisa ser inconfundível em relação ao
/// vermelho, sob pena de a faixa de margem parecer Zona de Perigo ao motorista
/// (RF03.4). Julgue os dois lado a lado, e sob sol direto.
class ModoCalibracao {
public:
    /// Quanto o duty anda por detente. 255 passos seriam finos demais para os
    /// ~20 detentes por volta do KY-040; com 5, a faixa inteira leva cerca de
    /// duas voltas e meia, que é manejável sem ser impreciso.
    static constexpr std::uint8_t kPasso = 5;

    /// Aplica um evento e devolve a cor resultante. `Nenhum` mantém tudo,
    /// para que o chamador possa invocar a cada ciclo sem piscar o LED.
    Cor aplica(EventoEncoder evento);

    Cor cor() const;
    ItemCalibracao item() const { return item_; }

    /// Duty do canal **variável** do item, de 0 a 255.
    std::uint8_t duty(ItemCalibracao item) const;

    /// O mesmo, de 0 a 1 — a forma de ler o resultado da calibração.
    float razao(ItemCalibracao item) const;

    /// Verdadeiro no evento em que o ciclo deu a volta e retornou ao primeiro
    /// item. É o momento natural de imprimir o resumo completo.
    bool completou_ciclo() const { return completou_ciclo_; }

private:
    std::uint8_t& variavel(ItemCalibracao item);
    const std::uint8_t& variavel(ItemCalibracao item) const;

    ItemCalibracao item_ = ItemCalibracao::Vermelho;
    bool completou_ciclo_ = false;

    // Cada item guarda o seu próprio ajuste, para que voltar atrás não perca
    // o que já foi escolhido.
    std::uint8_t vermelho_ = 255;
    std::uint8_t verde_    = 255;
    std::uint8_t azul_     = 255;
    // Iniciais dos compostos são os valores MEDIDOS em 2026-09-19, de
    // `led/Calibracao.h`: recalibrar parte de onde se chegou, não de um
    // palpite. Os palpites originais eram 115 e 153 — errados por 2,3x e
    // 3,8x, e teriam feito a busca começar longe do alvo.
    std::uint8_t ambar_verde_ = 50;
    std::uint8_t rosa_azul_   = 40;
};

}  // namespace coruja
