#include "display/VisorSt7789.h"

#include "display/DesenhaTexto.h"
#include "display/FonteNumero.h"
#include "display/FonteNumeroPequeno.h"
#include "display/FonteTexto.h"
#include "display/Sprites.h"

namespace coruja {


namespace {

}  // namespace

void VisorSt7789::retangulo(int x, int y, int largura, int altura,
                            Cor565 cor) {
    painel_.preenche(x, y, largura, altura, cor);
}

void VisorSt7789::texto(int x, int y, const char* texto, Fonte fonte,
                        Cor565 cor, Alinhamento alinhamento) {
    if (texto == nullptr || texto[0] == '\0') {
        return;
    }
    // `Centro` recebe o centro e `Esquerda` a borda -- a distinção está no
    // parâmetro e não no tipo de fonte, que era a armadilha que o
    // `Alinhamento` veio resolver (ver Visor.h).
    if (alinhamento == Alinhamento::Centro) {
        x -= largura_da_fonte(fonte, texto) / 2;
    }
    // O fundo vai junto com o glifo: uma escrita por caractere em vez de
    // apagar a região e escrever depois, o que se veria piscar.
    switch (fonte) {
        case Fonte::Numero:
            escreve_numero(painel_, x, y, texto, cor, paleta::kFundo);
            break;
        case Fonte::NumeroPequeno:
            escreve_numero_pequeno(painel_, x, y, texto, cor, paleta::kFundo);
            break;
        case Fonte::Texto:
            escreve_texto(painel_, x, y, texto, cor, paleta::kFundo);
            break;
        case Fonte::TextoGrande:
            escreve_texto_grande(painel_, x, y, texto, cor, paleta::kFundo);
            break;
    }
}

void VisorSt7789::icone(int x, int y, Icone icone) {
    const std::uint16_t* arte = nullptr;
    switch (icone) {
        case Icone::Nenhum:           return;
        case Icone::Radar:            arte = sprite::kRadar; break;
        case Icone::Semaforo:         arte = sprite::kSemaforo; break;
        case Icone::SemaforoComRadar: arte = sprite::kSemaforoComRadar; break;
    }
    painel_.desenha_rgb565(x, y, sprite::kLado, sprite::kLado, arte);
}

}  // namespace coruja
