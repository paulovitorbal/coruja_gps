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

/// De quanto em quanto o progresso vai para a tela.
///
/// **É uma decisão de desenho, não de rede.** O aparelho continua sabendo o
/// progresso exato — o que se arredonda é só o que se mostra.
///
/// O painel não tem buffer duplo: cada repintura APAGA a faixa e desenha por
/// cima, e o olho pega esse intervalo. A faixa do meio é a pior, porque leva
/// junto o rótulo "BAIXANDO", que não muda durante o download e mesmo assim
/// sumiria e voltaria a cada por cento — texto piscando incomoda bem mais do
/// que barra crescendo.
///
/// Em passos de 1% são ~100 repinturas num download; em passos de 5%, 21. E
/// 21 degraus ainda são mais do que a barra consegue mostrar: ela tem 256 px
/// de largura, então cada degrau são ~13 px, folgados para o olho perceber
/// que algo anda.
constexpr int kPassoProgressoPct = 5;

}  // namespace

void TelaOta::invalida() { anterior_ = Instantaneo{}; }

TelaOta::Instantaneo TelaOta::compoe(const EstadoOta& e) const {
    Instantaneo i;
    i.valido = true;

    if (e.rotulo != nullptr) {
        std::snprintf(i.rotulo, sizeof i.rotulo, "%s", e.rotulo);
    } else if (e.fase == FaseOta::Baixando && e.tentativa > 1) {
        // "BAIXANDO 2/3" diz que algo deu errado e está sendo refeito. Sem
        // isso uma retentativa parece travamento, e travamento é o que o
        // usuário faz quando desliga o aparelho no meio.
        std::snprintf(i.rotulo, sizeof i.rotulo, "%s %u/%u",
                      descreve(e.fase), e.tentativa, kTentativas);
    } else {
        std::snprintf(i.rotulo, sizeof i.rotulo, "%s", descreve(e.fase));
    }

    if (e.fase == FaseOta::Falhou && e.motivo != nullptr) {
        std::snprintf(i.motivo, sizeof i.motivo, "%s", e.motivo);
    }

    if (e.fase == e.fase_com_barra && e.total > 0) {
        const int pct = static_cast<int>(e.recebidos * 100U / e.total);
        const int limitado = pct > 100 ? 100 : pct;
        // Para BAIXO, nunca para o mais próximo: 49% virando 50% anunciaria
        // progresso que não houve. Como 100 é múltiplo de 5, o fim do
        // download continua chegando aos 100% exatos.
        i.barra_pct = limitado / kPassoProgressoPct * kPassoProgressoPct;
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
        std::strcmp(agora.valor, anterior_.valor) != 0 ||
        std::strcmp(agora.motivo, anterior_.motivo) != 0) {
        visor.retangulo(0, tela::kYAreaNumero, tela::kLargura,
                        tela::kAreaNumero, paleta::kFundo);

        const int y_rotulo = tela::kYAreaNumero + 30;
        // Vermelho só na palavra que diz que deu errado. O motivo, que é o
        // que se lê, fica branco: §4.1 manda distinguir por MATIZ e nunca
        // por luminância, porque o brilho do painel varia.
        const Cor565 cor_rotulo = agora.motivo[0] != '\0' ? paleta::kBarraPerigo
                                                          : paleta::kTexto;
        visor.texto(tela::kLargura / 2 -
                        largura_da_fonte(Fonte::Texto, agora.rotulo) / 2,
                    y_rotulo, agora.rotulo, Fonte::Texto, cor_rotulo,
                    Alinhamento::Esquerda);

        if (agora.motivo[0] != '\0') {
            visor.texto(tela::kLargura / 2 -
                            largura_da_fonte(Fonte::Texto, agora.motivo) / 2,
                        y_rotulo + altura_da_fonte(Fonte::Texto) + 16,
                        agora.motivo, Fonte::Texto, paleta::kTexto,
                        Alinhamento::Esquerda);
        }

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
