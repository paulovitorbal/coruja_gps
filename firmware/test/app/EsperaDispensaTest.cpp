#include "app/EsperaDispensa.h"

#include <gtest/gtest.h>

#include <vector>

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
    static constexpr int kTeto = 1000;

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

}  // namespace
