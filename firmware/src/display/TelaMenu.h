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
    /// Data da base, do cabeçalho do `radares.bin` (formato v2).
    ///
    /// **Não é a data da atualização, é a data dos DADOS.** O aparelho pode
    /// ter baixado hoje uma base de três meses atrás, e é a idade dos dados
    /// que diz se vale a pena atualizar — a do download não diz nada.
    ///
    /// Zero quando a base é do formato v1, que não a tinha.
    std::uint16_t ano = 0;
    std::uint8_t  mes = 0;
    std::uint8_t  dia = 0;
    std::size_t   pontos = 0;

    /// Taxa do GPS **congelada na entrada** da tela de informação.
    ///
    /// Era ao vivo, e estava errado: esta é a única tela do aparelho onde se
    /// lê em vez de relancear, e número tremendo enquanto se lê é ruído. A
    /// taxa instantânea tem lugar próprio na tela de dirigir.
    float         taxa_hz = 0.0F;

    /// Identificação do build, do `git describe`, mais a data.
    ///
    /// | O que aparece | O que significa |
    /// | :--- | :--- |
    /// | `v0.1.0` | exatamente na tag — firmware liberado |
    /// | `v0.1.0-3-ga996cb1` | três commits depois dela — build de trabalho |
    /// | `v0.1.0-3-ga996cb1*` | e com alterações não comitadas |
    ///
    /// **Existe por um incidente.** Em 2026-10-04 uma gravação não pegou, o
    /// aparelho ficou com firmware de cinco dias antes, e não havia como
    /// saber — nem o log do cartão distinguia. Meia hora foi gasta lendo
    /// código que estava correto.
    char          versao[24] = {};
};

/// Desenha o menu de ajustes do carro parado.
///
/// **Reaproveita as três faixas do §4.1 em vez de inventar um layout.** A
/// faixa superior diz onde se está, a área central mostra o item corrente
/// grande o bastante para ser lido sem óculos, e a faixa inferior mostra o
/// que o encoder faz agora. Quem abre o menu já conhece essa divisão da
/// tela de dirigir, e reaproveitá-la é uma coisa a menos para aprender.
///
/// **Sem rodapé de instruções** (decidido pelo autor em 2026-09-30). As
/// frases "girar: escolher / clicar: abrir" ocupavam a faixa inferior e
/// passavam de 320 px, o que as fazia rolar para dizer o que se aprende na
/// primeira vez que se usa o menu. O sublinhado do valor continua marcando
/// o modo de edição, que é a única distinção que não se adivinha.
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
    /// `agora_ms` entra porque a linha da base rola quando nao cabe, e
    /// rolagem depende do tempo e nao do conteudo.
    int desenha(const MenuAjustes& menu, const InfoAparelho& info,
                std::uint32_t agora_ms, Visor& visor);

    /// Força o próximo `desenha` a redesenhar tudo. Chamado ao abrir o
    /// menu, porque a tela por baixo era outra.
    void invalida();

private:
    struct Instantaneo {
        char       rotulo[24] = {};
        char       valor[16] = {};
        /// Quatro linhas de informação, só usadas no estado `Informando`.
        ///
        /// A data e a contagem de pontos ficam em linhas separadas (pedido
        /// do autor, 2026-09-30). Juntas davam 324 px numa tela de 320 —
        /// era a única linha do aparelho que precisava rolar, e rolar 4 px
        /// parece tremor, não rolagem. Separadas, as duas cabem.
        char       info[5][40] = {};
        EstadoMenu estado = EstadoMenu::Fechado;
        bool       valido = false;
    };

    Instantaneo compoe(const MenuAjustes& menu,
                       const InfoAparelho& info) const;

    Instantaneo anterior_;
    std::uint32_t agora_ms_ = 0;
    /// Deslocamento de cada linha de informacao, para detectar que ela
    /// andou e ter de redesenhar.
    int x_info_[4] = {0, 0, 0, 0};
};

}  // namespace coruja
