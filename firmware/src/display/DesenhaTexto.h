#pragma once
#include <cstdint>

#include "display/PainelSt7789.h"
#include "display/Visor.h"

namespace coruja {

/// Texto no painel com as fontes rasterizadas da JetBrains Mono.
///
/// Camada fina entre o `PainelSt7789` e as tabelas de glifos. Fica separada
/// porque o `Visor` completo -- que ainda precisa dos sprites de icone --
/// vai usar exatamente isto, e nao faria sentido escrever duas vezes.
///
/// **Monoespacada, e isso e recurso e nao limitacao.** A velocidade na tela
/// muda de 99 para 100 sem o numero dancar, porque cada digito ocupa a
/// mesma largura. Com fonte proporcional, o `1` estreito faria o conjunto
/// pular de lugar a cada mudanca -- ruim numa tela que se olha de relance.

/// Largura em pixels que o texto vai ocupar. Para centralizar antes de
/// desenhar, em vez de desenhar e descobrir depois.
int largura_numero(const char* texto);
int largura_numero_pequeno(const char* texto);
int largura_texto(const char* texto);

/// Desenha e devolve a largura consumida. `fundo` e pintado junto, o que
/// dispensa limpar a regiao antes -- uma escrita por glifo em vez de duas.
int escreve_numero(PainelSt7789& painel, int x, int y, const char* texto,
                   Cor565 cor, Cor565 fundo);
/// Metade da escala da velocidade, para o limite. A hierarquia e o que
/// permite "120/120" caber em 320 px -- ver a nota em `gera_fonte.py`.
int escreve_numero_pequeno(PainelSt7789& painel, int x, int y,
                           const char* texto, Cor565 cor, Cor565 fundo);
int escreve_texto(PainelSt7789& painel, int x, int y, const char* texto,
                  Cor565 cor, Cor565 fundo);

}  // namespace coruja
