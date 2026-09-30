#include "display/TextoRolante.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

TEST(TextoRolante, o_que_cabe_e_centralizado_e_nao_rola) {
    const Rolagem r = rolagem(100, 320, 0);
    EXPECT_EQ(r.x, 110);
    EXPECT_FALSE(r.rolando) << "rolou um texto que cabia";
}

TEST(TextoRolante, na_medida_exata_nao_rola) {
    // Fronteira: 320 de texto em 320 de tela cabe, e rolar ali seria
    // movimento sem informacao numa tela que se olha de relance.
    const Rolagem r = rolagem(320, 320, 0);
    EXPECT_EQ(r.x, 0);
    EXPECT_FALSE(r.rolando);
}

TEST(TextoRolante, um_pixel_a_mais_ja_rola) {
    EXPECT_TRUE(rolagem(321, 320, 0).rolando);
}

TEST(TextoRolante, comeca_parado_no_inicio_para_dar_tempo_de_ler) {
    // Rolagem continua obriga o olho a perseguir a primeira palavra.
    EXPECT_EQ(rolagem(432, 320, 0).x, 0);
    EXPECT_EQ(rolagem(432, 320, kPausaRolagemMs - 1).x, 0);
}

TEST(TextoRolante, depois_da_pausa_anda_de_um_caractere_por_vez) {
    // Passo de caractere e nao de pixel: cada passo redesenha a linha
    // inteira, e a 1 px/20 ms isso seriam 32% do SPI a 16 MHz -- bloqueio
    // que comeria a folga de 5,75 ms do encoder (R-36).
    EXPECT_EQ(rolagem(432, 320, kPausaRolagemMs).x, 0);
    EXPECT_EQ(rolagem(432, 320, kPausaRolagemMs + kMsPorPassoRolagem).x,
              -kPassoRolagemPx);
    EXPECT_EQ(rolagem(432, 320, kPausaRolagemMs + 3 * kMsPorPassoRolagem).x,
              -3 * kPassoRolagemPx);
}

TEST(TextoRolante, o_deslocamento_e_sempre_multiplo_do_passo_ou_o_curso) {
    // Fora do fim, o texto assenta em limite de caractere. No fim, ele para
    // no curso exato -- senao o ultimo trecho nunca apareceria.
    const int curso = 432 - 320;
    for (std::uint32_t t = 0; t < 30000; t += 13) {
        const int x = -rolagem(432, 320, t).x;
        const bool no_passo = (x % kPassoRolagemPx) == 0;
        EXPECT_TRUE(no_passo || x == curso) << "x=" << x << " em t=" << t;
    }
}

TEST(TextoRolante, para_quando_o_fim_do_texto_encosta_na_borda) {
    // **Nao passa do fim.** Continuar rolando deixaria a tela vazia por
    // alguns segundos, e vazio numa faixa de alerta e uma mensagem
    // diferente de "texto longo".
    const int curso = 432 - 320;
    const int passos = (curso + kPassoRolagemPx - 1) / kPassoRolagemPx;
    const std::uint32_t fim = kPausaRolagemMs +
                              static_cast<std::uint32_t>(passos) *
                                  kMsPorPassoRolagem;
    EXPECT_EQ(rolagem(432, 320, fim).x, -curso);
    EXPECT_EQ(rolagem(432, 320, fim + 500).x, -curso);
}

TEST(TextoRolante, o_ciclo_repete) {
    const int curso = 432 - 320;
    const int passos = (curso + kPassoRolagemPx - 1) / kPassoRolagemPx;
    const std::uint32_t ciclo =
        kPausaRolagemMs + static_cast<std::uint32_t>(passos) *
                              kMsPorPassoRolagem + kPausaRolagemMs;
    EXPECT_EQ(rolagem(432, 320, ciclo).x, rolagem(432, 320, 0).x);
    EXPECT_EQ(rolagem(432, 320, ciclo + 7 * ciclo).x,
              rolagem(432, 320, 0).x);
}

TEST(TextoRolante, nunca_mostra_o_fim_emendado_no_comeco) {
    // Rolagem em anel emenda o fim da frase no comeco dela, e por um
    // instante se le uma frase que nao existe. Aqui o x fica sempre entre
    // 0 e -curso: nunca ha duas copias do texto na tela.
    const int curso = 432 - 320;
    for (std::uint32_t t = 0; t < 60000; t += 37) {
        const Rolagem r = rolagem(432, 320, t);
        EXPECT_LE(r.x, 0);
        EXPECT_GE(r.x, -curso) << "passou do fim em t=" << t;
    }
}

TEST(TextoRolante, texto_muito_maior_que_a_tela_ainda_rola_inteiro) {
    // Uma frase de 80 caracteres em corpo de 12 px da 960 px. O curso e
    // maior, o ciclo e mais longo, e o fim continua alcancavel.
    const int curso = 960 - 320;
    const int passos = (curso + kPassoRolagemPx - 1) / kPassoRolagemPx;
    const std::uint32_t fim = kPausaRolagemMs +
                              static_cast<std::uint32_t>(passos) *
                                  kMsPorPassoRolagem;
    EXPECT_EQ(rolagem(960, 320, fim).x, -curso);
}

}  // namespace
