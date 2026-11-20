#include "app/EsperaDispensa.h"

#include <gtest/gtest.h>

#include <vector>

#include "apoio/CaoDeGuardaFalso.h"
#include "apoio/EncoderMock.h"

namespace {

using namespace coruja;

class PausaFalsa : public Pausa {
public:
    std::vector<std::uint32_t> esperas;
    std::uint32_t relogio = 0;

    void espera_ms(std::uint32_t ms) override {
        esperas.push_back(ms);
        relogio += ms;
    }
    std::uint32_t agora_ms() override { return relogio; }
};

/// Conta as batidas e solta um evento na N-esima delas -- e assim que o
/// usuario mexe no encoder "durante" a espera.
///
/// **Tem freio de mao, e ele ganhou o lugar na primeira passada de mutacao.**
/// O `ate_dispensar` so sai quando um evento aparece, entao um defeito que
/// faca o evento ser ignorado nao quebra o teste: PENDURA. Teste pendurado
/// nao diz o que houve e ainda trava a suite inteira. Com o freio, o mesmo
/// defeito vira uma falha com nome.
class BatimentoEspiao : public Batimento {
public:
    static constexpr int kTeto = 5000;

    BatimentoEspiao(teste::EncoderMock* enc, int na_batida,
                    EventoEncoder e = EventoEncoder::Clique)
        : enc_(enc), na_batida_(na_batida), evento_(e) {}

    void mantem() override {
        ++batidas;
        if (batidas == na_batida_) { enc_->enfileira(evento_); }
        if (batidas >= kTeto) {
            // Solta um clique a forca so para a espera terminar; quem
            // verifica o teto e a asercao de quem chamou.
            estourou = true;
            enc_->enfileira(EventoEncoder::Clique);
        }
    }

    int  batidas = 0;
    bool estourou = false;

private:
    teste::EncoderMock* enc_;
    int                 na_batida_;
    EventoEncoder       evento_;
};

/// Roda a espera e exige que ela tenha saido pelo evento, e nao pelo freio.
void dispensa(EsperaDispensa& espera, BatimentoEspiao& bat) {
    espera.ate_dispensar(bat);
    ASSERT_FALSE(bat.estourou) << "a espera nao reconheceu o evento";
}

TEST(EsperaDispensa, o_clique_dispensa) {
    teste::EncoderMock enc;
    PausaFalsa pausa;
    BatimentoEspiao bat(&enc, 3, EventoEncoder::Clique);
    EsperaDispensa espera(enc, pausa);

    dispensa(espera, bat);

    EXPECT_EQ(bat.batidas, 3);
}

TEST(EsperaDispensa, o_giro_tambem_dispensa) {
    // Quem acabou de ver uma falha mexe no que tiver na mao. Exigir
    // justamente o clique transformaria o giro em "nao funcionou".
    for (const auto giro : {EventoEncoder::GiroDireita,
                            EventoEncoder::GiroEsquerda}) {
        teste::EncoderMock enc;
        PausaFalsa pausa;
        BatimentoEspiao bat(&enc, 2, giro);
        EsperaDispensa espera(enc, pausa);

        dispensa(espera, bat);

        EXPECT_EQ(bat.batidas, 2) << "giro " << nome_evento(giro);
    }
}

TEST(EsperaDispensa, continua_pintando_enquanto_espera) {
    // **A razao de a espera nao ser um `sleep`.** O LED pisca por TEMPO, e
    // durante o OTA ninguem mais chama nada: sem bater aqui, o LED
    // congelaria no meio de um piscar e a tela pareceria travada -- que e
    // exatamente o que a tela de atualizacao existe para desmentir.
    teste::EncoderMock enc;
    PausaFalsa pausa;
    BatimentoEspiao bat(&enc, 10, EventoEncoder::Clique);
    EsperaDispensa espera(enc, pausa);

    dispensa(espera, bat);

    EXPECT_EQ(bat.batidas, 10);
    EXPECT_EQ(pausa.esperas.size(), 9u) << "esperou sem pintar, ou pintou sem esperar";
}

TEST(EsperaDispensa, o_passo_e_curto_o_bastante_para_o_piscar) {
    // O LED pisca a 2 Hz na falha: meio ciclo sao 250 ms. Um passo maior
    // do que isso pularia estados do piscar e o LED ficaria errático.
    teste::EncoderMock enc;
    PausaFalsa pausa;
    BatimentoEspiao bat(&enc, 4, EventoEncoder::Clique);
    EsperaDispensa espera(enc, pausa);

    dispensa(espera, bat);

    ASSERT_FALSE(pausa.esperas.empty());
    for (const auto ms : pausa.esperas) { EXPECT_LE(ms, 100u); }
}

TEST(EsperaDispensa, descarta_o_que_ja_estava_na_fila) {
    // **O defeito que isto evita, e ele e certo de acontecer:** quem chega
    // nesta tela chegou por um CLIQUE, e o usuario clicou de novo enquanto o
    // OTA rodava -- o pedido veio de alguem que clicou duas vezes. Sem
    // limpar, o evento velho dispensaria a tela antes de ela ser lida, e a
    // falha pareceria nao ter sido mostrada.
    teste::EncoderMock enc({EventoEncoder::Clique, EventoEncoder::GiroDireita});
    PausaFalsa pausa;
    BatimentoEspiao bat(&enc, 5, EventoEncoder::Clique);
    EsperaDispensa espera(enc, pausa);

    dispensa(espera, bat);

    EXPECT_EQ(bat.batidas, 5) << "um evento velho dispensou a tela";
}

TEST(EsperaDispensa, pinta_pelo_menos_uma_vez) {
    // Mesmo que o usuario dispense no mesmo instante, a tela tem de ir ao
    // painel: quem chama acabou de mudar o estado e ninguem mais vai pintar.
    teste::EncoderMock enc;
    PausaFalsa pausa;
    BatimentoEspiao bat(&enc, 1, EventoEncoder::Clique);
    EsperaDispensa espera(enc, pausa);

    dispensa(espera, bat);

    EXPECT_EQ(bat.batidas, 1);
    EXPECT_TRUE(pausa.esperas.empty()) << "dormiu sem precisar";
}

TEST(EsperaDispensa, alimenta_o_cao_a_cada_pintura) {
    // **A espera que o usuario encerra.** Sem alimentar aqui, o aparelho
    // reiniciaria 16,5 s depois de mostrar o motivo de uma falha -- e o
    // motivo e tudo o que o usuario recebe para decidir o que tentar.
    //
    // Uma refeicao por PINTURA, e nao por volta de relogio: o `mantem()` e
    // uma transacao com o painel. Um display travado nao fecha a volta, e o
    // cao morde.
    teste::EncoderMock enc;
    PausaFalsa pausa;
    teste::CaoDeGuardaFalso cao;
    BatimentoEspiao bat(&enc, 7, EventoEncoder::Clique);
    EsperaDispensa espera(enc, pausa, &cao);

    dispensa(espera, bat);

    EXPECT_EQ(cao.alimentacoes, static_cast<unsigned>(bat.batidas));
}

TEST(EsperaDispensa, sem_ninguem_para_dispensar_ela_se_dispensa_no_teto) {
    // **A unica espera que dependia so do usuario, e ela segurava o aparelho
    // MUDO**: enquanto espera, `app.passo`, o GPS e os alertas nao rodam. Com
    // o cao alimentado a cada pintura, nem o cao mordia. Sem evento nenhum,
    // tem de acabar sozinha.
    teste::EncoderMock enc;
    PausaFalsa pausa;
    teste::CaoDeGuardaFalso cao;
    BatimentoEspiao bat(&enc, 0, EventoEncoder::Clique);   // nunca solta
    EsperaDispensa espera(enc, pausa, &cao);

    espera.ate_dispensar(bat);

    EXPECT_FALSE(bat.estourou) << "so o freio do teste encerrou a espera";
    EXPECT_GE(pausa.relogio, EsperaDispensa::kTetoMs);
    EXPECT_LE(pausa.relogio, EsperaDispensa::kTetoMs + 100u)
        << "passou do teto por mais que um passo";
}

TEST(EsperaDispensa, o_teto_conta_desde_a_entrada_e_nao_desde_o_boot) {
    // O relogio real ja esta longe de zero quando a falha aparece.
    teste::EncoderMock enc;
    PausaFalsa pausa;
    pausa.relogio = 3'600'000;
    BatimentoEspiao bat(&enc, 0, EventoEncoder::Clique);
    EsperaDispensa espera(enc, pausa);

    espera.ate_dispensar(bat);

    EXPECT_FALSE(bat.estourou);
    EXPECT_GE(pausa.relogio - 3'600'000u, EsperaDispensa::kTetoMs);
    EXPECT_LE(pausa.relogio - 3'600'000u, EsperaDispensa::kTetoMs + 100u);
}

TEST(EsperaDispensa, o_usuario_dispensa_bem_antes_do_teto) {
    teste::EncoderMock enc;
    PausaFalsa pausa;
    BatimentoEspiao bat(&enc, 20, EventoEncoder::Clique);
    EsperaDispensa espera(enc, pausa);

    dispensa(espera, bat);

    EXPECT_LT(pausa.relogio, EsperaDispensa::kTetoMs);
}

TEST(EsperaDispensa, sem_cao_a_espera_roda_igual) {
    teste::EncoderMock enc;
    PausaFalsa pausa;
    BatimentoEspiao bat(&enc, 3, EventoEncoder::Clique);
    EsperaDispensa espera(enc, pausa);

    dispensa(espera, bat);

    EXPECT_EQ(bat.batidas, 3);
}

}  // namespace
