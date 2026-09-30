#pragma once
#include "display/PainelSt7789.h"
#include "display/Visor.h"

namespace coruja {

/// O `Visor` do §4.1 sobre o painel real.
///
/// É a ponte que faltava: a `TelaPrincipal` e o `TelaMenu` decidem **o que**
/// mostrar e são testados no host contra um dublê; esta classe é a única
/// que sabe que existe um ST7789V do outro lado.
///
/// **`apresenta()` não faz nada, e isso é projeto e não omissão.** Não há
/// framebuffer: o §4.1 mostra que o layout não muda entre estados e que
/// nada pisca, então se desenha por região e cada chamada já foi ao painel.
/// O método existe porque a interface o prevê, e um dia um painel com buffer
/// duplo pode precisar dele.
class VisorSt7789 final : public Visor {
public:
    explicit VisorSt7789(PainelSt7789& painel) : painel_(painel) {}

    void retangulo(int x, int y, int largura, int altura, Cor565 cor) override;
    void texto(int x, int y, const char* texto, Fonte fonte, Cor565 cor,
               Alinhamento alinhamento) override;
    void icone(int x, int y, Icone icone) override;
    void apresenta() override {}

private:
    PainelSt7789& painel_;
};

}  // namespace coruja
