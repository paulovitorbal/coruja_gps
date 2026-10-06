#include "display/TelaMenu.h"

#include <cstdio>
#include <cstring>

#include "display/FonteNumeroPequeno.h"
#include "display/TextoRolante.h"

namespace coruja {

namespace {

void copia(char* destino, std::size_t n, const char* origem) {
    std::snprintf(destino, n, "%s", origem);
}

/// Com que fonte desenhar o valor do item.
///
/// ⚠️ **`NumeroPequeno` só tem `0123456789/`** — onze glifos. Valor com letra
/// desenhado com ela sai **invisível**, e foi exatamente o que acontecia:
///
///   - `viagem` mostrava o rótulo e mais nada;
///   - `modo noturno` idem, com `auto` / `dia` / `noite`;
///   - `brilho` mostrava `80` porque o `%` também não existe na fonte.
///
/// Relatado dirigindo, em 2026-10-06. Não aparecia em teste nenhum porque a
/// suíte compara o TEXTO composto, e o texto sempre esteve certo — quem some é
/// o pixel.
///
/// A escolha é por conteúdo e não por item: fonte é propriedade de quem
/// desenha, e uma tabela item→fonte teria de ser mantida em sincronia com os
/// rótulos para sempre.
Fonte fonte_do_valor(const char* v) {
    for (const char* c = v; *c != '\0'; ++c) {
        // Consulta o mapa da PROPRIA fonte, em vez de repetir a lista aqui.
        // Se o conjunto de glifos mudar — e ele mudou, para ganhar o "%" —,
        // esta decisao acompanha sozinha.
        if (std::strchr(fonte::numeropequeno::kMapa, *c) == nullptr) {
            return Fonte::Texto;
        }
    }
    return Fonte::NumeroPequeno;
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
        if (info.ano != 0) {
            // dd/mm/aa: dois dígitos no ano porque a linha é lida de
            // relance e o século não acrescenta nada.
            std::snprintf(i.info[1], sizeof i.info[1], "base: %02u/%02u/%02u",
                          static_cast<unsigned>(info.dia),
                          static_cast<unsigned>(info.mes),
                          static_cast<unsigned>(info.ano % 100U));
        } else {
            // Base no formato antigo, que não trazia data. Dizer "sem data"
            // é melhor que inventar uma ou deixar a linha vazia.
            std::snprintf(i.info[1], sizeof i.info[1], "base: sem data");
        }
        std::snprintf(i.info[2], sizeof i.info[2], "%u pontos",
                      static_cast<unsigned>(info.pontos));
        // Uma casa decimal: a diferença entre 4,0 e 3,6 Hz é o que separa
        // "normal" de "degradado" no RF01.5, e sem a decimal os dois
        // apareceriam como 4.
        std::snprintf(i.info[3], sizeof i.info[3], "gps: %.1f Hz",
                      static_cast<double>(info.taxa_hz));
        // Por ultimo porque e a linha que menos se consulta -- e a que mais
        // importa quando algo nao bate com o que se espera do firmware.
        std::snprintf(i.info[4], sizeof i.info[4], "fw: %s",
                      info.versao[0] != '\0' ? info.versao : "?");
    }

    return i;
}

int TelaMenu::desenha(const MenuAjustes& menu, const InfoAparelho& info,
                      std::uint32_t agora_ms, Visor& visor) {
    agora_ms_ = agora_ms;
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
    for (int l = 0; l < 4; ++l) {
        info_mudou = info_mudou ||
                     std::strcmp(agora.info[l], anterior_.info[l]) != 0;
    }
    bool info_rolando = false;
    if (agora.estado == EstadoMenu::Informando) {
        for (int l = 0; l < 4; ++l) {
            const Rolagem r =
                rolagem(largura_da_fonte(Fonte::Texto, agora.info[l]),
                        tela::kLargura, agora_ms);
            if (r.rolando) {
                info_rolando = info_rolando || r.x != x_info_[l];
                x_info_[l] = r.x;
            }
        }
    }
    if (tudo || info_mudou || info_rolando ||
        std::strcmp(agora.rotulo, anterior_.rotulo) != 0 ||
        std::strcmp(agora.valor, anterior_.valor) != 0 ||
        agora.estado != anterior_.estado) {
        visor.retangulo(0, tela::kYAreaNumero, tela::kLargura,
                        tela::kAreaNumero, paleta::kFundo);

        if (agora.estado == EstadoMenu::Informando) {
            // Três linhas em corpo de texto: aqui se lê, não se relanceia.
            // É a única tela do aparelho com essa premissa, e ela só é
            // válida porque o carro está parado.
            // Cinco linhas centralizadas na area: 5x20 de altura mais
            // 4x10 de vao dao 140 px nos 166 disponiveis.
            const int passo = altura_da_fonte(Fonte::Texto) + 10;
            const int altura_bloco = 5 * altura_da_fonte(Fonte::Texto) +
                                     4 * 10;
            int y = tela::kYAreaNumero + (tela::kAreaNumero - altura_bloco) / 2;
            for (int l = 0; l < 5; ++l) {
                // A linha da base passa de 320 px com uma versao datada e
                // 18 mil pontos, e e a unica desta tela que rola. Cabendo,
                // `rolagem` devolve o `x` centralizado.
                const Rolagem r =
                    rolagem(largura_da_fonte(Fonte::Texto, agora.info[l]),
                            tela::kLargura, agora_ms_);
                visor.texto(r.x, y, agora.info[l], Fonte::Texto,
                            paleta::kTexto, Alinhamento::Esquerda);
                y += passo;
            }
            ++regioes;
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
            const Fonte fonte = fonte_do_valor(agora.valor);
            const int y_valor = y_rotulo + altura_da_fonte(Fonte::Texto) + 18;
            visor.texto(tela::kLargura / 2, y_valor, agora.valor,
                        fonte, paleta::kTexto, Alinhamento::Centro);

            // **Em edição, o valor ganha um sublinhado.** Distinguir o modo
            // por cor violaria a regra 1 da paleta — o PWM do backlight
            // apaga luminância —, e por isso a marca é de forma: uma barra
            // que não depende de o olho comparar dois brancos.
            if (agora.estado == EstadoMenu::Editando) {
                // A mesma fonte do valor: medir com a outra poria o
                // sublinhado com largura e altura de um texto que nao esta ali.
                const int l = largura_da_fonte(fonte, agora.valor);
                visor.retangulo((tela::kLargura - l) / 2,
                                y_valor + altura_da_fonte(fonte) + 6,
                                l, 4, paleta::kTexto);
            }
        }
        ++regioes;
    }

    if (regioes > 0) { visor.apresenta(); }
    anterior_ = agora;
    return regioes;
}

}  // namespace coruja
