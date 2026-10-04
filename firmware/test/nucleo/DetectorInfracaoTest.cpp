#include "nucleo/DetectorInfracao.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

Ponto radar(float lat, std::uint8_t limite) {
    return Ponto{lat, -44.0F, limite, 0, TipoPonto::RadarFixo,
                 Sentido::Omnidirecional};
}

Telemetria em(float velocidade_kmh) {
    Telemetria t;
    t.lat = -19.8F;
    t.lon = -44.0F;
    t.velocidade_kmh = velocidade_kmh;
    t.rumo_graus = 90.0F;
    t.rumo_valido = true;
    t.ano = 2026; t.mes = 10; t.dia = 4;
    t.hora = 17; t.minuto = 31; t.segundo = 12;
    t.data_valida = true;
    return t;
}

/// Um ciclo com o radar visivel a `dist` metros.
Veredito vendo(const Ponto& p, float dist) {
    Veredito v;
    v.tem_mais_proximo = true;
    v.mais_proximo = p;
    v.dist_mais_proximo_m = dist;
    return v;
}

/// Um ciclo sem ponto nenhum a vista -- e o que acontece ao passar pelo
/// radar, porque o filtro de "a frente" o descarta na perpendicular.
Veredito nada() { return Veredito{}; }

// --- o caminho que gera registro ---

TEST(DetectorInfracao, passar_perto_acima_do_v_infra_registra) {
    const auto p = radar(-19.79F, 60);   // V_infra = 66
    DetectorInfracao d;
    RegistroInfracao r;

    EXPECT_FALSE(d.alimenta(vendo(p, 250.0F), em(70.0F), &r));
    EXPECT_FALSE(d.alimenta(vendo(p, 120.0F), em(72.0F), &r));
    EXPECT_FALSE(d.alimenta(vendo(p, 12.0F), em(71.0F), &r));
    ASSERT_TRUE(d.alimenta(nada(), em(68.0F), &r)) << "a passagem fechou e nao registrou";

    EXPECT_FLOAT_EQ(r.dist_min_m, 12.0F);
    EXPECT_FLOAT_EQ(r.momento.velocidade_kmh, 71.0F) << "a velocidade NA passagem";
    EXPECT_FLOAT_EQ(r.v_max_kmh, 72.0F) << "a maxima da aproximacao";
    EXPECT_FLOAT_EQ(r.v_infra_kmh, 66.0F);
    EXPECT_EQ(r.radar.limite, 60);
}

TEST(DetectorInfracao, o_registro_guarda_a_posicao_do_veiculo_NA_passagem) {
    // Nao a posicao de quando a aproximacao fechou: entre uma e outra o carro
    // ja andou, e o que interessa e onde ele estava diante do radar.
    const auto p = radar(-19.79F, 60);
    DetectorInfracao d;
    RegistroInfracao r;

    auto perto = em(71.0F);
    perto.lat = -19.7901F;
    perto.segundo = 12;
    d.alimenta(vendo(p, 8.0F), perto, &r);

    auto depois = em(70.0F);
    depois.lat = -19.7850F;    // ja passou, 50 m adiante
    depois.segundo = 14;
    ASSERT_TRUE(d.alimenta(nada(), depois, &r));

    EXPECT_FLOAT_EQ(r.momento.lat, -19.7901F);
    EXPECT_EQ(r.momento.segundo, 12);
}

// --- os caminhos que NAO geram registro ---

TEST(DetectorInfracao, passar_perto_dentro_do_limite_nao_registra) {
    const auto p = radar(-19.79F, 60);
    DetectorInfracao d;
    RegistroInfracao r;

    d.alimenta(vendo(p, 150.0F), em(60.0F), &r);
    d.alimenta(vendo(p, 10.0F), em(64.0F), &r);   // 64 < V_infra de 66
    EXPECT_FALSE(d.alimenta(nada(), em(62.0F), &r));
}

TEST(DetectorInfracao, correr_e_reduzir_antes_do_radar_nao_registra) {
    // O caso que o alerta existe para produzir: entrou a 95, viu o aviso,
    // passou a 62. Nao houve infracao, e o arquivo nao deve dizer que houve.
    const auto p = radar(-19.79F, 60);
    DetectorInfracao d;
    RegistroInfracao r;

    d.alimenta(vendo(p, 280.0F), em(95.0F), &r);
    d.alimenta(vendo(p, 100.0F), em(80.0F), &r);
    d.alimenta(vendo(p, 9.0F), em(62.0F), &r);
    EXPECT_FALSE(d.alimenta(nada(), em(60.0F), &r))
        << "reduziu a tempo, nao pode registrar";
}

TEST(DetectorInfracao, passar_longe_acima_do_v_infra_nao_registra) {
    // Via paralela: nunca chegou aos 50 m, entao nao passou por este radar.
    const auto p = radar(-19.79F, 60);
    DetectorInfracao d;
    RegistroInfracao r;

    d.alimenta(vendo(p, 200.0F), em(90.0F), &r);
    d.alimenta(vendo(p, 85.0F), em(90.0F), &r);
    EXPECT_FALSE(d.alimenta(nada(), em(90.0F), &r));
}

TEST(DetectorInfracao, semaforo_sem_limite_nunca_registra) {
    // Camera de semaforo nao afere velocidade (RF03.3). V_infra vale zero
    // nela, e comparar contra zero poria toda passagem em infracao.
    const auto p = radar(-19.79F, kSemLimite);
    DetectorInfracao d;
    RegistroInfracao r;

    d.alimenta(vendo(p, 100.0F), em(90.0F), &r);
    d.alimenta(vendo(p, 5.0F), em(90.0F), &r);
    EXPECT_FALSE(d.alimenta(nada(), em(90.0F), &r));
}

TEST(DetectorInfracao, nao_registra_enquanto_a_aproximacao_corre) {
    // So fecha no fim: registrar a cada ciclo daria 4 linhas por segundo.
    const auto p = radar(-19.79F, 60);
    DetectorInfracao d;
    RegistroInfracao r;

    for (float dist = 300.0F; dist > 5.0F; dist -= 10.0F) {
        EXPECT_FALSE(d.alimenta(vendo(p, dist), em(90.0F), &r))
            << "registrou a " << dist << " m, antes de fechar";
    }
}

TEST(DetectorInfracao, o_pico_pode_estar_no_PRIMEIRO_ciclo_da_aproximacao) {
    // Sair de uma curva a 95 com o radar ja em alcance e frear dali em
    // diante: a maxima da aproximacao e a primeira leitura, e nenhuma depois
    // a supera. Sem semear `v_max` na abertura, o pico se perde -- e os
    // outros testes nao pegam, porque neles a velocidade sobe antes de cair.
    const auto p = radar(-19.79F, 60);
    DetectorInfracao d;
    RegistroInfracao r;

    d.alimenta(vendo(p, 290.0F), em(95.0F), &r);
    d.alimenta(vendo(p, 150.0F), em(85.0F), &r);
    d.alimenta(vendo(p, 10.0F), em(70.0F), &r);
    ASSERT_TRUE(d.alimenta(nada(), em(68.0F), &r));

    EXPECT_FLOAT_EQ(r.v_max_kmh, 95.0F) << "perdeu o pico do primeiro ciclo";
    EXPECT_FLOAT_EQ(r.momento.velocidade_kmh, 70.0F);
}

TEST(DetectorInfracao, uma_aproximacao_de_um_ciclo_so_ainda_registra) {
    // Radar que aparece e some entre duas leituras -- a 4 Hz e 110 km/h sao
    // 7,6 m por amostra, entao um ponto rente a pista pode ser visto uma vez
    // so. A abertura precisa deixar o detector ja completo.
    const auto p = radar(-19.79F, 60);
    DetectorInfracao d;
    RegistroInfracao r;

    d.alimenta(vendo(p, 15.0F), em(88.0F), &r);
    ASSERT_TRUE(d.alimenta(nada(), em(88.0F), &r));

    EXPECT_FLOAT_EQ(r.dist_min_m, 15.0F);
    EXPECT_FLOAT_EQ(r.v_max_kmh, 88.0F);
    EXPECT_FLOAT_EQ(r.momento.velocidade_kmh, 88.0F);
}

TEST(DetectorInfracao, exatamente_no_v_infra_nao_e_infracao) {
    // A fronteira. V_infra e o limiar ACIMA do qual se multa -- em cima dele
    // ainda nao ha infracao, e e a mesma regra que o `e_perigo` do Zonamento
    // usa. Um `<` no lugar do `<=` passaria despercebido sem este caso.
    const auto p = radar(-19.79F, 60);   // V_infra = 66,0 exatos
    DetectorInfracao d;
    RegistroInfracao r;

    d.alimenta(vendo(p, 10.0F), em(66.0F), &r);
    EXPECT_FALSE(d.alimenta(nada(), em(66.0F), &r));

    // E um decimo acima ja e.
    DetectorInfracao d2;
    d2.alimenta(vendo(p, 10.0F), em(66.1F), &r);
    EXPECT_TRUE(d2.alimenta(nada(), em(66.1F), &r));
}

TEST(DetectorInfracao, guarda_a_distancia_MINIMA_e_nao_a_ultima) {
    // A distancia nem sempre cai monotonicamente ate o fim: abaixo de
    // 5 km/h, ou com rumo invalido, o filtro de "a frente" ABRE e o ponto
    // continua visivel ja tendo sido ultrapassado. Guardar a ultima leitura
    // em vez da menor registraria a velocidade do lugar errado -- e poderia
    // reprovar o corte de 50 m com um radar pelo qual se passou a 20 m.
    const auto p = radar(-19.79F, 60);
    DetectorInfracao d;
    RegistroInfracao r;

    d.alimenta(vendo(p, 100.0F), em(70.0F), &r);
    d.alimenta(vendo(p, 20.0F), em(71.0F), &r);   // o momento da passagem
    d.alimenta(vendo(p, 35.0F), em(69.0F), &r);   // ja afastando
    d.alimenta(vendo(p, 60.0F), em(68.0F), &r);
    ASSERT_TRUE(d.alimenta(nada(), em(68.0F), &r));

    EXPECT_FLOAT_EQ(r.dist_min_m, 20.0F) << "guardou a ultima, nao a minima";
    EXPECT_FLOAT_EQ(r.momento.velocidade_kmh, 71.0F);
}

TEST(DetectorInfracao, afastando_sem_nunca_chegar_perto_nao_registra) {
    // Mesmo mecanismo, do outro lado: se a minima ficou acima dos 50 m, nao
    // houve passagem, por mais que a leitura final esteja longe.
    const auto p = radar(-19.79F, 60);
    DetectorInfracao d;
    RegistroInfracao r;

    d.alimenta(vendo(p, 90.0F), em(90.0F), &r);
    d.alimenta(vendo(p, 70.0F), em(90.0F), &r);
    d.alimenta(vendo(p, 120.0F), em(90.0F), &r);
    EXPECT_FALSE(d.alimenta(nada(), em(90.0F), &r));
}

TEST(DetectorInfracao, destino_nulo_nao_quebra) {
    const auto p = radar(-19.79F, 60);
    DetectorInfracao d;

    d.alimenta(vendo(p, 10.0F), em(90.0F), nullptr);
    EXPECT_TRUE(d.alimenta(nada(), em(90.0F), nullptr));
}

// --- varios radares ---

TEST(DetectorInfracao, dois_radares_em_sequencia_dao_dois_registros) {
    const auto a = radar(-19.790F, 60);
    const auto b = radar(-19.780F, 40);   // V_infra = 46
    DetectorInfracao d;
    RegistroInfracao r;

    d.alimenta(vendo(a, 100.0F), em(70.0F), &r);
    d.alimenta(vendo(a, 10.0F), em(71.0F), &r);

    // O mais proximo passa a ser o B: fecha o A na mesma chamada.
    ASSERT_TRUE(d.alimenta(vendo(b, 200.0F), em(70.0F), &r));
    EXPECT_EQ(r.radar.limite, 60) << "o primeiro registro e do radar A";

    d.alimenta(vendo(b, 8.0F), em(55.0F), &r);
    ASSERT_TRUE(d.alimenta(nada(), em(50.0F), &r));
    EXPECT_EQ(r.radar.limite, 40) << "o segundo registro e do radar B";
    EXPECT_FLOAT_EQ(r.momento.velocidade_kmh, 55.0F);
}

TEST(DetectorInfracao, trocar_de_radar_nao_mistura_a_velocidade_maxima) {
    // A maxima e POR aproximacao. Vazar a do radar anterior inflaria o
    // registro seguinte com um pico que aconteceu noutro lugar.
    const auto a = radar(-19.790F, 60);
    const auto b = radar(-19.780F, 40);
    DetectorInfracao d;
    RegistroInfracao r;

    d.alimenta(vendo(a, 10.0F), em(120.0F), &r);   // pico altissimo no A
    d.alimenta(vendo(b, 200.0F), em(50.0F), &r);   // fecha o A, comeca o B
    d.alimenta(vendo(b, 8.0F), em(55.0F), &r);
    ASSERT_TRUE(d.alimenta(nada(), em(50.0F), &r));

    EXPECT_FLOAT_EQ(r.v_max_kmh, 55.0F) << "o pico de 120 era do radar A";
}

TEST(DetectorInfracao, reinicia_esquece_a_aproximacao_em_curso) {
    const auto p = radar(-19.79F, 60);
    DetectorInfracao d;
    RegistroInfracao r;

    d.alimenta(vendo(p, 10.0F), em(90.0F), &r);
    d.reinicia();
    EXPECT_FALSE(d.alimenta(nada(), em(90.0F), &r));
}

}  // namespace
