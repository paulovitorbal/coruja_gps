#include "display/TelaMenu.h"

#include <cstdio>
#include <cstring>

namespace coruja {

namespace {

void copia(char* destino, std::size_t n, const char* origem) {
    std::snprintf(destino, n, "%s", origem);
}

}  // namespace

void TelaMenu::invalida() { anterior_ = Instantaneo{}; }

TelaMenu::Instantaneo TelaMenu::compoe(const MenuAjustes& menu,
                                       const InfoAparelho& info) const {
    Instantaneo i;
    i.valido = true;
    i.estado = menu.estado();
    copia(i.rotulo, sizeof i.rotulo, menu.rotulo(menu.item()));
    menu.valor(menu.item(), i.valor, sizeof i.valor);

    if (menu.estado() == EstadoMenu::Informando) {
        // O nome vem primeiro: com duas unidades, saber QUAL aparelho se
        // está olhando vale mais que qualquer outro número aqui.
        std::snprintf(i.info[0], sizeof i.info[0], "unidade: %s",
                      info.nome[0] != '\0' ? info.nome : "sem nome");
        std::snprintf(i.info[1], sizeof i.info[1], "base: %s  %u pts",
                      info.versao_base[0] != '\0' ? info.versao_base : "?",
                      static_cast<unsigned>(info.pontos));
        // Uma casa decimal: a diferença entre 4,0 e 3,6 Hz é o que separa
        // "normal" de "degradado" no RF01.5, e sem a decimal os dois
        // apareceriam como 4.
        std::snprintf(i.info[2], sizeof i.info[2], "gps: %.1f Hz",
                      static_cast<double>(info.taxa_hz));
    }

    // O rodapé diz o que o encoder faz **agora**, e muda com o estado. É a
    // única pista de que girar deixou de navegar e passou a editar — sem
    // ela, o usuário descobriria a diferença mexendo, que num menu que
    // altera brilho e volume significa mexer no que não queria.
    switch (menu.estado()) {
        case EstadoMenu::Navegando:
            copia(i.rodape, sizeof i.rodape, "girar: escolher   clicar: abrir");
            break;
        case EstadoMenu::Editando:
            copia(i.rodape, sizeof i.rodape, "girar: mudar   clicar: confirmar");
            break;
        case EstadoMenu::Informando:
            copia(i.rodape, sizeof i.rodape, "qualquer acao volta");
            break;
        case EstadoMenu::Fechado:
            break;
    }
    return i;
}

int TelaMenu::desenha(const MenuAjustes& menu, const InfoAparelho& info,
                      Visor& visor) {
    const Instantaneo agora = compoe(menu, info);
    const bool tudo = !anterior_.valido;
    int regioes = 0;

    if (tudo) {
        visor.retangulo(0, 0, tela::kLargura, tela::kAltura, paleta::kFundo);
        ++regioes;
    }

    // Faixa superior: onde se está. Fixa enquanto o menu existir, e é por
    // isso que só aparece no redesenho completo.
    if (tudo) {
        visor.texto(tela::kLargura / 2, tela::kYFaixaSuperior + 3, "AJUSTES",
                    Fonte::Texto, paleta::kTexto, Alinhamento::Centro);
        ++regioes;
    }

    bool info_mudou = false;
    for (int l = 0; l < 3; ++l) {
        info_mudou = info_mudou ||
                     std::strcmp(agora.info[l], anterior_.info[l]) != 0;
    }
    if (tudo || info_mudou ||
        std::strcmp(agora.rotulo, anterior_.rotulo) != 0 ||
        std::strcmp(agora.valor, anterior_.valor) != 0 ||
        agora.estado != anterior_.estado) {
        visor.retangulo(0, tela::kYAreaNumero, tela::kLargura,
                        tela::kAreaNumero, paleta::kFundo);

        if (agora.estado == EstadoMenu::Informando) {
            // Três linhas em corpo de texto: aqui se lê, não se relanceia.
            // É a única tela do aparelho com essa premissa, e ela só é
            // válida porque o carro está parado.
            const int passo = altura_da_fonte(Fonte::Texto) + 10;
            int y = tela::kYAreaNumero + 30;
            for (int l = 0; l < 3; ++l) {
                visor.texto(tela::kLargura / 2, y, agora.info[l],
                            Fonte::Texto, paleta::kTexto,
                            Alinhamento::Centro);
                y += passo;
            }
            ++regioes;
            if (tudo || std::strcmp(agora.rodape, anterior_.rodape) != 0) {
                visor.retangulo(0, tela::kYFaixaInferior, tela::kLargura,
                                tela::kFaixaInferior, paleta::kFundo);
                visor.texto(tela::kLargura / 2, tela::kYFaixaInferior + 14,
                            agora.rodape, Fonte::Texto, paleta::kTexto,
                            Alinhamento::Centro);
                ++regioes;
            }
            visor.apresenta();
            anterior_ = agora;
            return regioes;
        }

        // Rótulo acima, valor abaixo e maior. A hierarquia é a mesma da
        // tela de dirigir: o que muda ao girar fica grande.
        const int y_rotulo = tela::kYAreaNumero + 24;
        visor.texto(tela::kLargura / 2, y_rotulo, agora.rotulo, Fonte::Texto,
                    paleta::kTexto, Alinhamento::Centro);

        if (agora.valor[0] != '\0') {
            const int y_valor = y_rotulo + altura_da_fonte(Fonte::Texto) + 18;
            visor.texto(tela::kLargura / 2, y_valor, agora.valor,
                        Fonte::NumeroPequeno, paleta::kTexto,
                        Alinhamento::Centro);

            // **Em edição, o valor ganha um sublinhado.** Distinguir o modo
            // por cor violaria a regra 1 da paleta — o PWM do backlight
            // apaga luminância —, e por isso a marca é de forma: uma barra
            // que não depende de o olho comparar dois brancos.
            if (agora.estado == EstadoMenu::Editando) {
                const int l = largura_da_fonte(Fonte::NumeroPequeno,
                                               agora.valor);
                visor.retangulo((tela::kLargura - l) / 2,
                                y_valor + altura_da_fonte(Fonte::NumeroPequeno)
                                    + 6,
                                l, 4, paleta::kTexto);
            }
        }
        ++regioes;
    }

    if (tudo || std::strcmp(agora.rodape, anterior_.rodape) != 0) {
        visor.retangulo(0, tela::kYFaixaInferior, tela::kLargura,
                        tela::kFaixaInferior, paleta::kFundo);
        visor.texto(tela::kLargura / 2, tela::kYFaixaInferior + 14,
                    agora.rodape, Fonte::Texto, paleta::kTexto,
                    Alinhamento::Centro);
        ++regioes;
    }

    if (regioes > 0) { visor.apresenta(); }
    anterior_ = agora;
    return regioes;
}

}  // namespace coruja
