#pragma once
#include <cstddef>
#include <cstdint>

#include "display/Visor.h"
#include "menu/MenuAjustes.h"

namespace coruja {

/// O que a tela de informação mostra.
///
/// **Vem de fora porque o `MenuAjustes` não sabe nada disto** — e não deve
/// saber. Ele decide navegação; a versão da base, a contagem de pontos e a
/// taxa do GPS pertencem a três subsistemas diferentes, e dar a ele
/// ponteiros para os três só para exibir texto inverteria as dependências.
struct InfoAparelho {
    char          nome[24] = {};      ///< qual das duas unidades é esta
    char          versao_base[24] = {};
    std::size_t   pontos = 0;
    float         taxa_hz = 0.0F;
};

/// Desenha o menu de ajustes do carro parado.
///
/// **Reaproveita as três faixas do §4.1 em vez de inventar um layout.** A
/// faixa superior diz onde se está, a área central mostra o item corrente
/// grande o bastante para ser lido sem óculos, e a faixa inferior mostra o
/// que o encoder faz agora. Quem abre o menu já conhece essa divisão da
/// tela de dirigir, e reaproveitá-la é uma coisa a menos para aprender.
///
/// **O item corrente ocupa o centro sozinho, e não há lista rolando.** Uma
/// lista de sete itens em 166 px caberia em corpo pequeno, e o menu é usado
/// com o carro parado mas com o motorista ainda ao volante — provavelmente
/// de noite, provavelmente com pressa. Um item por vez, grande, é mais
/// lento de percorrer e mais difícil de errar.
///
/// Só redesenha o que mudou, como a `TelaPrincipal`, e pela mesma razão.
class TelaMenu {
public:
    /// Devolve quantas regiões foram tocadas; zero quando nada mudou.
    int desenha(const MenuAjustes& menu, const InfoAparelho& info,
                Visor& visor);

    /// Força o próximo `desenha` a redesenhar tudo. Chamado ao abrir o
    /// menu, porque a tela por baixo era outra.
    void invalida();

private:
    struct Instantaneo {
        char       rotulo[24] = {};
        char       valor[16] = {};
        /// Três linhas de informação, só usadas no estado `Informando`.
        char       info[3][32] = {};
        char       rodape[40] = {};
        EstadoMenu estado = EstadoMenu::Fechado;
        bool       valido = false;
    };

    Instantaneo compoe(const MenuAjustes& menu,
                       const InfoAparelho& info) const;

    Instantaneo anterior_;
};

}  // namespace coruja
