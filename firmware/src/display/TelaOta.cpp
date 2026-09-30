#include "display/TelaOta.h"

#include <cstdio>
#include <cstring>

namespace coruja {

namespace {

/// Mesma geometria da barra de distância da `TelaPrincipal`: 28 px de altura
/// em x=56. A posição é a mensagem — "quanto falta" mora sempre aqui.
constexpr int kBarraX = 56;
constexpr int kBarraL = tela::kLargura - kBarraX - 8;
constexpr int kBarraA = 28;

}  // namespace

void TelaOta::invalida() { anterior_ = Instantaneo{}; }

TelaOta::Instantaneo TelaOta::compoe(const EstadoOta& e) const {
    Instantaneo i;
    i.valido = true;

    if (e.fase == FaseOta::Baixando && e.tentativa > 1) {
        // "BAIXANDO 2/3" diz que algo deu errado e está sendo refeito. Sem
        // isso uma retentativa parece travamento, e travamento é o que o
        // usuário faz quando desliga o aparelho no meio.
        std::snprintf(i.rotulo, sizeof i.rotulo, "%s %u/%u",
                      descreve(e.fase), e.tentativa, kTentativas);
    } else {
        std::snprintf(i.rotulo, sizeof i.rotulo, "%s", descreve(e.fase));
    }

    if (e.fase == FaseOta::Baixando && e.total > 0) {
        const int pct = static_cast<int>(e.recebidos * 100U / e.total);
        i.barra_pct = pct > 100 ? 100 : pct;
        std::snprintf(i.valor, sizeof i.valor, "%d%%", i.barra_pct);
    }
    return i;
}

int TelaOta::desenha(const EstadoOta& estado, Visor& visor) {
    const Instantaneo agora = compoe(estado);
    const bool tudo = !anterior_.valido;
    int regioes = 0;

    if (tudo) {
        visor.retangulo(0, 0, tela::kLargura, tela::kAltura, paleta::kFundo);
        visor.texto(tela::kLargura / 2 -
                        largura_da_fonte(Fonte::Texto, "ATUALIZANDO") / 2,
                    tela::kYFaixaSuperior + 3, "ATUALIZANDO", Fonte::Texto,
                    paleta::kTexto, Alinhamento::Esquerda);
        ++regioes;
    }

    if (tudo || std::strcmp(agora.rotulo, anterior_.rotulo) != 0 ||
        std::strcmp(agora.valor, anterior_.valor) != 0) {
        visor.retangulo(0, tela::kYAreaNumero, tela::kLargura,
                        tela::kAreaNumero, paleta::kFundo);

        const int y_rotulo = tela::kYAreaNumero + 30;
        visor.texto(tela::kLargura / 2 -
                        largura_da_fonte(Fonte::Texto, agora.rotulo) / 2,
                    y_rotulo, agora.rotulo, Fonte::Texto, paleta::kTexto,
                    Alinhamento::Esquerda);

        if (agora.valor[0] != '\0') {
            const int l = largura_da_fonte(Fonte::NumeroPequeno, agora.valor);
            visor.texto((tela::kLargura - l) / 2,
                        y_rotulo + altura_da_fonte(Fonte::Texto) + 20,
                        agora.valor, Fonte::NumeroPequeno, paleta::kTexto,
                        Alinhamento::Esquerda);
        }
        ++regioes;
    }

    if (tudo || agora.barra_pct != anterior_.barra_pct) {
        visor.retangulo(0, tela::kYFaixaInferior, tela::kLargura,
                        tela::kFaixaInferior, paleta::kFundo);
        if (agora.barra_pct >= 0) {
            const int y = tela::kYFaixaInferior + 8;
            visor.retangulo(kBarraX, y, kBarraL, kBarraA, paleta::kMoldura);
            const int cheio = kBarraL * agora.barra_pct / 100;
            if (cheio > 0) {
                // Âmbar e não verde: verde diria "pronto", e a barra fala do
                // que está em curso. O verde fica para o fim, no LED.
                visor.retangulo(kBarraX, y, cheio, kBarraA,
                                paleta::kBarraAmbar);
            }
        }
        ++regioes;
    }

    if (regioes > 0) { visor.apresenta(); }
    anterior_ = agora;
    return regioes;
}

}  // namespace coruja
