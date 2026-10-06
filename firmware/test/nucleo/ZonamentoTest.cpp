#include "nucleo/Zonamento.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "nucleo/Geo.h"
#include "nucleo/LimiarInfracao.h"

namespace {

using namespace coruja;

// Um trecho do Eixão, que é a rota do simulador. A latitude negativa importa:
// um sinal trocado na conversão metros→graus inverteria norte e sul e faria
// todos os testes de "à frente" passarem pelo motivo errado.
constexpr float kLat0 = -15.79F;
constexpr float kLon0 = -47.88F;

float cos0() { return geo::cosseno_latitude(kLat0); }

/// Rumo de filtragem conhecido. Explícito nos testes de geometria: eles
/// respondem "este ponto está à frente de quem aponta para N graus?", e essa
/// pergunta não depende de velocidade nem de memória.
RumoFiltro rf(float graus) { return RumoFiltro{true, graus}; }

/// Rumo indisponível — o caso em que os filtros têm de ABRIR.
RumoFiltro rf_aberto() { return RumoFiltro{}; }

/// Desloca em metros a partir da origem. Usa a mesma constante e o mesmo
/// cosseno que a máquina, então a distância volta exata o bastante para
/// testar fronteiras de 1 m.
float lat_deslocada(float metros_norte) {
    return kLat0 + metros_norte / geo::kMetrosPorGrauLat;
}
float lon_deslocada(float metros_leste) {
    return kLon0 + metros_leste / (geo::kMetrosPorGrauLat * cos0());
}

Ponto faz_ponto(float norte_m, float leste_m, std::uint8_t limite,
                Sentido sentido = Sentido::Omnidirecional,
                std::uint16_t rumo_graus = 0) {
    Ponto p{};
    p.lat = lat_deslocada(norte_m);
    p.lon = lon_deslocada(leste_m);
    p.limite = limite;
    p.rumo_q = static_cast<std::uint8_t>(rumo_graus / 2);
    p.tipo = (limite == kSemLimite) ? TipoPonto::SemaforoCamera
                                    : TipoPonto::RadarFixo;
    p.sentido = sentido;
    return p;
}

/// A busca binária do RF02.1 exige a base ordenada por latitude. O conversor
/// garante isso; aqui garantimos igual, senão o teste mediria outra coisa.
std::vector<Ponto> base_de(std::vector<Ponto> pontos) {
    std::sort(pontos.begin(), pontos.end(),
              [](const Ponto& a, const Ponto& b) { return a.lat < b.lat; });
    return pontos;
}

/// Veículo na origem, rumo norte por omissão — todo ponto ao norte está à
/// frente, e ao sul, para trás.
Telemetria veiculo(float velocidade_kmh, float rumo_graus = 0.0F,
                   float norte_m = 0.0F) {
    Telemetria t{};
    t.lat = lat_deslocada(norte_m);
    t.lon = kLon0;
    t.velocidade_kmh = velocidade_kmh;
    t.rumo_graus = rumo_graus;
    t.rumo_valido = true;
    return t;
}

// ===================================================== RF02.1 busca binária

TEST(PrimeiroComLatGe, base_vazia_devolve_zero) {
    EXPECT_EQ(primeiro_com_lat_ge(nullptr, 0, kLat0), 0U);
}

TEST(PrimeiroComLatGe, todos_maiores_devolve_zero) {
    const auto base = base_de({faz_ponto(100, 0, 60), faz_ponto(200, 0, 60)});
    EXPECT_EQ(primeiro_com_lat_ge(base.data(), base.size(), kLat0), 0U);
}

TEST(PrimeiroComLatGe, todos_menores_devolve_o_tamanho) {
    const auto base = base_de({faz_ponto(-200, 0, 60), faz_ponto(-100, 0, 60)});
    EXPECT_EQ(primeiro_com_lat_ge(base.data(), base.size(), kLat0), 2U);
}

TEST(PrimeiroComLatGe, encontra_a_primeira_de_varias_latitudes_iguais) {
    const auto base = base_de({faz_ponto(-50, 0, 60), faz_ponto(0, 10, 60),
                               faz_ponto(0, 20, 60), faz_ponto(50, 0, 60)});
    EXPECT_EQ(primeiro_com_lat_ge(base.data(), base.size(), kLat0), 1U);
}

TEST(PrimeiroComLatGe, acha_o_limite_inferior_em_base_grande) {
    std::vector<Ponto> pontos;
    for (int i = 0; i < 2000; ++i) {
        pontos.push_back(faz_ponto(static_cast<float>(i) * 100.0F, 0, 60));
    }
    const auto base = base_de(pontos);
    // 50 km ao norte = ponto de índice 500.
    const std::size_t i =
        primeiro_com_lat_ge(base.data(), base.size(), lat_deslocada(50000.0F));
    EXPECT_EQ(i, 500U);
}

// ================================================== RF03.1 ponto está à frente

TEST(PontoAFrente, ponto_ao_norte_com_veiculo_rumo_norte) {
    EXPECT_TRUE(ponto_a_frente(rf(0.0F), veiculo(60), faz_ponto(200, 0, 60), cos0()));
}

TEST(PontoAFrente, ponto_ao_sul_ja_foi_ultrapassado) {
    // O requisito existe por isto: sem ele o buzzer seguiria tocando às
    // costas do radar, porque a distância continua abaixo de 300 m.
    EXPECT_FALSE(ponto_a_frente(rf(0.0F), veiculo(60), faz_ponto(-200, 0, 60), cos0()));
}

TEST(PontoAFrente, perpendicular_conta_como_a_frente_no_limite) {
    // Exatamente 90°: a fronteira é inclusiva.
    EXPECT_TRUE(ponto_a_frente(rf(0.0F), veiculo(60), faz_ponto(0, 200, 60), cos0()));
}

TEST(PontoAFrente, logo_atras_do_perpendicular_ja_e_descartado) {
    EXPECT_FALSE(ponto_a_frente(rf(0.0F), veiculo(60), faz_ponto(-20, 200, 60), cos0()));
}

TEST(PontoAFrente, abre_sem_rumo_utilizavel) {
    // Sem rumo não há por onde decidir, e abrir é a falha segura: esconder um
    // radar à frente é perigoso, mostrar um que ficou para trás é irritante.
    //
    // ⚠️ QUEM decide que o rumo é inutilizável é o `rumo_para_filtro`, e isso
    // tem testes próprios. Aqui se verifica só o que este filtro faz quando
    // recebe a resposta.
    EXPECT_TRUE(ponto_a_frente(rf_aberto(), veiculo(60),
                               faz_ponto(-200, 0, 60), cos0()));
    EXPECT_FALSE(ponto_a_frente(rf(0.0F), veiculo(60),
                                faz_ponto(-200, 0, 60), cos0()));
}

TEST(PontoAFrente, em_cima_do_ponto_o_azimute_nao_existe) {
    // atan2(0,0) devolveria zero e descartaria o alvo por um ângulo inventado.
    Telemetria t = veiculo(60, 180.0F);
    EXPECT_TRUE(ponto_a_frente(rf(t.rumo_graus), t, faz_ponto(0, 0, 60), cos0()));
}

// ================================================ RF02.3 filtro de sentido

TEST(SentidoCompativel, omnidirecional_nunca_descarta) {
    const Ponto p = faz_ponto(200, 0, 60, Sentido::Omnidirecional, 0);
    EXPECT_TRUE(sentido_compativel(rf(180.0F), p));
}

TEST(SentidoCompativel, unidirecional_aceita_ate_trinta_graus) {
    const Ponto p = faz_ponto(200, 0, 60, Sentido::Unidirecional, 0);
    EXPECT_TRUE(sentido_compativel(rf(0.0F), p));
    EXPECT_TRUE(sentido_compativel(rf(30.0F), p));
    EXPECT_TRUE(sentido_compativel(rf(330.0F), p));  // cruza o zero
    EXPECT_FALSE(sentido_compativel(rf(31.0F), p));
}

TEST(SentidoCompativel, unidirecional_recusa_o_sentido_oposto) {
    const Ponto p = faz_ponto(200, 0, 60, Sentido::Unidirecional, 0);
    EXPECT_FALSE(sentido_compativel(rf(180.0F), p));
}

TEST(SentidoCompativel, bidirecional_aceita_o_sentido_oposto) {
    const Ponto p = faz_ponto(200, 0, 60, Sentido::Bidirecional, 0);
    EXPECT_TRUE(sentido_compativel(rf(180.0F), p));
    EXPECT_TRUE(sentido_compativel(rf(150.0F), p));  // 30° do oposto
    EXPECT_FALSE(sentido_compativel(rf(149.0F), p));
}

TEST(SentidoCompativel, bidirecional_recusa_a_transversal) {
    const Ponto p = faz_ponto(200, 0, 60, Sentido::Bidirecional, 0);
    EXPECT_FALSE(sentido_compativel(rf(90.0F), p));
    EXPECT_FALSE(sentido_compativel(rf(45.0F), p));
}

TEST(SentidoCompativel, abre_sem_rumo_utilizavel) {
    const Ponto p = faz_ponto(200, 0, 60, Sentido::Unidirecional, 0);
    EXPECT_TRUE(sentido_compativel(rf_aberto(), p));
    EXPECT_FALSE(sentido_compativel(rf(180.0F), p));
}

// ========================================= rumo lembrado com veículo parado
//
// A regra nasceu de um defeito observado dirigindo, em 2026-10-06: passar por
// um radar, parar num semáforo 200 m adiante, e ver na tela o radar já
// ultrapassado. Com velocidade abaixo do piso, os dois filtros abriam e todo
// ponto a menos de 300 m voltava a ser candidato.

TEST(RumoParaFiltro, usa_o_rumo_medido_quando_ha_velocidade) {
    MaquinaZona m;
    const RumoFiltro r = m.rumo_para_filtro(veiculo(60, 90.0F), cos0());
    EXPECT_TRUE(r.utilizavel);
    EXPECT_FLOAT_EQ(r.graus, 90.0F);
}

TEST(RumoParaFiltro, abre_se_nunca_houve_rumo_bom) {
    MaquinaZona m;
    // Ligar o aparelho já parado: não há o que lembrar.
    EXPECT_FALSE(m.rumo_para_filtro(veiculo(0, 0.0F), cos0()).utilizavel);
}

TEST(RumoParaFiltro, lembra_o_rumo_com_o_veiculo_parado_no_mesmo_lugar) {
    MaquinaZona m;
    m.rumo_para_filtro(veiculo(60, 90.0F), cos0());

    Telemetria parado = veiculo(0, 0.0F);
    parado.rumo_valido = false;        // RMC vem sem rumo com o carro parado
    const RumoFiltro r = m.rumo_para_filtro(parado, cos0());

    EXPECT_TRUE(r.utilizavel);
    EXPECT_FLOAT_EQ(r.graus, 90.0F);   // o de antes, não o zero do campo vazio
}

TEST(RumoParaFiltro, esquece_se_o_veiculo_andou_sem_rumo_novo) {
    MaquinaZona m;
    m.rumo_para_filtro(veiculo(60, 90.0F), cos0());

    // Andou 26 m a passo, sem nunca cruzar o piso de velocidade: pode ter
    // mudado de direção, e a certeza acaba.
    Telemetria longe = veiculo(0, 0.0F);
    longe.rumo_valido = false;
    longe.lat = lat_deslocada(26.0F);
    EXPECT_FALSE(m.rumo_para_filtro(longe, cos0()).utilizavel);
}

TEST(RumoParaFiltro, a_fronteira_dos_vinte_e_cinco_metros) {
    for (const auto& caso : {std::pair<float, bool>{25.0F, true},
                             std::pair<float, bool>{25.1F, false}}) {
        MaquinaZona m;
        m.rumo_para_filtro(veiculo(60, 90.0F), cos0());
        Telemetria t = veiculo(0, 0.0F);
        t.rumo_valido = false;
        t.lat = lat_deslocada(caso.first);
        EXPECT_EQ(m.rumo_para_filtro(t, cos0()).utilizavel, caso.second)
            << "deslocamento de " << caso.first << " m";
    }
}

TEST(RumoParaFiltro, esquecer_e_definitivo_ate_vir_rumo_novo) {
    MaquinaZona m;
    m.rumo_para_filtro(veiculo(60, 90.0F), cos0());

    Telemetria longe = veiculo(0, 0.0F);
    longe.rumo_valido = false;
    longe.lat = lat_deslocada(26.0F);
    m.rumo_para_filtro(longe, cos0());          // esquece aqui

    // Voltar para perto da posição antiga não ressuscita a memória: o veículo
    // andou, e isso basta para a direção ser desconhecida.
    Telemetria perto = veiculo(0, 0.0F);
    perto.rumo_valido = false;
    EXPECT_FALSE(m.rumo_para_filtro(perto, cos0()).utilizavel);
}

TEST(RumoParaFiltro, reinicia_apaga_a_memoria) {
    MaquinaZona m;
    m.rumo_para_filtro(veiculo(60, 90.0F), cos0());
    m.reinicia();
    Telemetria parado = veiculo(0, 0.0F);
    parado.rumo_valido = false;
    EXPECT_FALSE(m.rumo_para_filtro(parado, cos0()).utilizavel);
}

TEST(SemaforoDepoisDoRadar, radar_ultrapassado_nao_volta_com_o_carro_parado) {
    // ⚠️ ESTE É O DEFEITO RELATADO. Reproduz o trajeto real de 2026-10-06:
    // passar por um radar fixo a 60 km/h e parar num semáforo 200 m adiante.
    //
    // Antes da memória de rumo, a parada fazia os filtros abrirem e o radar
    // às costas voltava a ser candidato — a tela mostrava um radar que já
    // tinha ficado para trás.
    const auto base = base_de({faz_ponto(0, 0, 60)});   // radar na origem
    MaquinaZona m;

    // Aproximação, passagem e desaceleração. As amostras acompanham o
    // trajeto porque o receptor entrega 4 por segundo — a 60 km/h são 4 m
    // entre elas.
    //
    // ⚠️ Isto não é enfeite do teste. Saltar de 120 m direto para 200 m faria
    // o guarda dos 25 m descartar a memória, e com razão: deslocamento sem
    // rumo novo é exatamente o que ele existe para detectar. A primeira versão
    // deste teste fazia esse salto e falhava — o cenário é que estava errado.
    for (float norte : {-250.0F, -120.0F, -30.0F, 20.0F, 120.0F, 175.0F, 198.0F}) {
        m.avalia(veiculo(60.0F, 0.0F, norte), base.data(), base.size(), 1000);
    }

    // Semáforo 200 m depois: o RMC para de informar rumo.
    Telemetria parado = veiculo(0.0F, 0.0F, 200.0F);
    parado.rumo_valido = false;
    const Veredito v =
        m.avalia(parado, base.data(), base.size(), 20000);

    EXPECT_EQ(v.zona, Zona::Segura);
    EXPECT_FALSE(v.tem_alvo);
}

TEST(SemaforoDepoisDoRadar, radar_a_frente_continua_aparecendo_parado) {
    // O contrapeso: parar ANTES do radar não pode esconder o alerta. Se a
    // memória de rumo escondesse isto, teríamos trocado um incômodo por um
    // perigo.
    const auto base = base_de({faz_ponto(0, 0, 60)});
    MaquinaZona m;

    for (float norte : {-250.0F, -220.0F, -202.0F}) {
        m.avalia(veiculo(60.0F, 0.0F, norte), base.data(), base.size(), 1000);
    }

    Telemetria parado = veiculo(0.0F, 0.0F, -200.0F);
    parado.rumo_valido = false;
    const Veredito v = m.avalia(parado, base.data(), base.size(), 20000);

    EXPECT_NE(v.zona, Zona::Segura);
    EXPECT_TRUE(v.tem_alvo);
}

// ====================================================== RF03 zonas básicas

TEST(MaquinaZona, base_vazia_e_zona_segura) {
    MaquinaZona m;
    const Veredito v = m.avalia(veiculo(80), nullptr, 0, 0);
    EXPECT_EQ(v.zona, Zona::Segura);
    EXPECT_FALSE(v.tem_alvo);
    EXPECT_EQ(v.faixa, FaixaSonora::Nenhuma);
}

TEST(MaquinaZona, ponto_alem_de_trezentos_metros_nao_alerta) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(301, 0, 60)});
    EXPECT_EQ(m.avalia(veiculo(80), base.data(), base.size(), 0).zona,
              Zona::Segura);
}

TEST(MaquinaZona, dentro_de_trezentos_metros_e_abaixo_do_limite_e_conforme) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(299, 0, 60)});
    const Veredito v = m.avalia(veiculo(55), base.data(), base.size(), 0);
    EXPECT_EQ(v.zona, Zona::AproximacaoConforme);
    EXPECT_TRUE(v.tem_alvo);
    EXPECT_NEAR(v.distancia_m, 299.0F, 1.0F);
    EXPECT_EQ(v.faixa, FaixaSonora::Nenhuma);
    EXPECT_NEAR(v.v_infra_kmh, 66.0F, 0.01F);
}

TEST(MaquinaZona, entre_o_limite_e_o_v_infra_e_margem_e_silenciosa) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(200, 0, 60)});
    const Veredito v = m.avalia(veiculo(63), base.data(), base.size(), 0);
    EXPECT_EQ(v.zona, Zona::AproximacaoMargem);
    EXPECT_EQ(v.faixa, FaixaSonora::Nenhuma);  // RF03.9: margem não soa
}

TEST(MaquinaZona, acima_do_v_infra_e_perigo_com_som) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(200, 0, 60)});
    const Veredito v = m.avalia(veiculo(67), base.data(), base.size(), 0);
    EXPECT_EQ(v.zona, Zona::Perigo);
    EXPECT_EQ(v.faixa, FaixaSonora::Lenta);
}

TEST(MaquinaZona, o_v_infra_e_seis_km_h_e_nao_o_limite_legal_de_sete) {
    // RF03.6: 6 km/h e 5% são escolhas deliberadas do projeto, mais
    // conservadoras que os 7 km/h e 7% do CONTRAN. Não "corrigir".
    MaquinaZona m;
    const auto base = base_de({faz_ponto(200, 0, 60)});
    EXPECT_EQ(m.avalia(veiculo(66.0F), base.data(), base.size(), 0).zona,
              Zona::AproximacaoMargem);
    EXPECT_EQ(m.avalia(veiculo(66.5F), base.data(), base.size(), 0).zona,
              Zona::Perigo);
}

TEST(MaquinaZona, acima_de_cem_o_desconto_vira_percentual) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(200, 0, 120)});
    // V_infra = 120 * 1.05 = 126, não 126.
    EXPECT_EQ(m.avalia(veiculo(125.0F), base.data(), base.size(), 0).zona,
              Zona::AproximacaoMargem);
    EXPECT_EQ(m.avalia(veiculo(127.0F), base.data(), base.size(), 0).zona,
              Zona::Perigo);
}

// ========================================================= RF03.3 semáforo

TEST(MaquinaZona, limite_zero_e_zona_de_semaforo) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(200, 0, kSemLimite)});
    const Veredito v = m.avalia(veiculo(80), base.data(), base.size(), 0);
    EXPECT_EQ(v.zona, Zona::Semaforo);
    EXPECT_EQ(v.faixa, FaixaSonora::Nenhuma);
    EXPECT_FLOAT_EQ(v.v_infra_kmh, 0.0F);
}

TEST(MaquinaZona, semaforo_nunca_vira_perigo_por_mais_rapido_que_se_va) {
    // `velocidade_infracao(0)` devolve zero, e comparar a velocidade contra
    // zero poria todo semáforo em Perigo. Teste de mutação confirma que
    // remover a guarda de `kSemLimite` em `e_perigo` faz este caso falhar.
    MaquinaZona m;
    const auto base = base_de({faz_ponto(200, 0, kSemLimite)});
    const Veredito v = m.avalia(veiculo(300.0F), base.data(), base.size(), 0);
    EXPECT_EQ(v.zona, Zona::Semaforo);
    EXPECT_EQ(v.faixa, FaixaSonora::Nenhuma);
}

TEST(MaquinaZona, semaforo_nao_herda_o_perigo_de_outro_ponto) {
    // A histerese consulta a zona anterior. Com a máquina já em Perigo, um
    // semáforo isolado não pode ser arrastado junto.
    MaquinaZona m;
    const auto radar = base_de({faz_ponto(200, 0, 60)});
    ASSERT_EQ(m.avalia(veiculo(90), radar.data(), radar.size(), 0).zona,
              Zona::Perigo);
    const auto semaforo = base_de({faz_ponto(200, 0, kSemLimite)});
    EXPECT_EQ(m.avalia(veiculo(90), semaforo.data(), semaforo.size(), 250).zona,
              Zona::Semaforo);
}

// ============================================ RF03.5 radar móvel como fixo

TEST(MaquinaZona, radar_movel_zona_igual_a_de_um_radar_fixo) {
    MaquinaZona m;
    auto p = faz_ponto(200, 0, 60);
    p.tipo = TipoPonto::RadarMovel;
    const auto base = base_de({p});
    const Veredito v = m.avalia(veiculo(80), base.data(), base.size(), 0);
    EXPECT_EQ(v.zona, Zona::Perigo);
    EXPECT_EQ(v.alvo.tipo, TipoPonto::RadarMovel);
}

// =================================== RF03.2 histerese de distância 300/340

TEST(MaquinaZona, alvo_retido_entre_trezentos_e_trezentos_e_quarenta) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(0, 0, 60)});  // radar na origem

    // Aproxima-se por trás: veículo 250 m ao sul do radar, rumo norte.
    Telemetria t = veiculo(55, 0.0F, -250.0F);
    ASSERT_EQ(m.avalia(t, base.data(), base.size(), 0).zona,
              Zona::AproximacaoConforme);

    // Recua para 320 m: acima do raio de entrada, abaixo do de saída.
    t = veiculo(55, 0.0F, -320.0F);
    EXPECT_EQ(m.avalia(t, base.data(), base.size(), 250).zona,
              Zona::AproximacaoConforme);

    // Passa de 340 m: solta.
    t = veiculo(55, 0.0F, -341.0F);
    EXPECT_EQ(m.avalia(t, base.data(), base.size(), 500).zona, Zona::Segura);
}

TEST(MaquinaZona, alvo_retido_tambem_no_eixo_leste_oeste) {
    // A janela de longitude da varredura tem de usar o raio de saída, igual à
    // de latitude. Testar a histerese só no eixo norte-sul deixa passar uma
    // janela estreita demais, porque ali a longitude nunca aperta.
    MaquinaZona m;
    const auto base = base_de({faz_ponto(0, 0, 60)});

    Telemetria t = veiculo(55, 90.0F);  // rumo leste
    t.lon = lon_deslocada(-250.0F);
    ASSERT_TRUE(m.avalia(t, base.data(), base.size(), 0).tem_alvo);

    t.lon = lon_deslocada(-320.0F);
    EXPECT_TRUE(m.avalia(t, base.data(), base.size(), 250).tem_alvo);

    t.lon = lon_deslocada(-341.0F);
    EXPECT_EQ(m.avalia(t, base.data(), base.size(), 500).zona, Zona::Segura);
}

TEST(MaquinaZona, ponto_que_nunca_entrou_nao_ganha_a_histerese) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(0, 0, 60)});
    const Telemetria t = veiculo(55, 0.0F, -320.0F);
    EXPECT_EQ(m.avalia(t, base.data(), base.size(), 0).zona, Zona::Segura);
}

TEST(MaquinaZona, alvo_solta_ao_ser_ultrapassado_mesmo_perto) {
    // "Sai acima de 340 m **ou passou**". Passar é o caso comum: a 60 km/h o
    // radar fica para trás muito antes de ficar longe.
    MaquinaZona m;
    const auto base = base_de({faz_ponto(0, 0, 60)});
    ASSERT_TRUE(m.avalia(veiculo(55, 0.0F, -100.0F), base.data(), base.size(), 0)
                    .tem_alvo);
    EXPECT_EQ(m.avalia(veiculo(55, 0.0F, 50.0F), base.data(), base.size(), 250)
                  .zona,
              Zona::Segura);
}

// ====================================== RF03.2/RF03.7 histerese de velocidade

TEST(MaquinaZona, perigo_so_solta_dois_km_h_abaixo_do_v_infra) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(200, 0, 60)});  // V_infra = 66
    const Ponto* b = base.data();
    const std::size_t n = base.size();

    ASSERT_EQ(m.avalia(veiculo(67), b, n, 0).zona, Zona::Perigo);
    EXPECT_EQ(m.avalia(veiculo(65), b, n, 250).zona, Zona::Perigo);
    EXPECT_EQ(m.avalia(veiculo(64.5F), b, n, 500).zona, Zona::Perigo);
    EXPECT_EQ(m.avalia(veiculo(63.5F), b, n, 750).zona,
              Zona::AproximacaoMargem);
}

TEST(MaquinaZona, faixas_sonoras_ancoradas_no_v_infra) {
    // V_infra = 66 → rápida acima de 72.6, pulso acima de 79.2 (RF03.7).
    MaquinaZona m;
    const auto base = base_de({faz_ponto(200, 0, 60)});
    const Ponto* b = base.data();
    const std::size_t n = base.size();

    EXPECT_EQ(m.avalia(veiculo(67), b, n, 0).faixa, FaixaSonora::Lenta);
    EXPECT_EQ(m.avalia(veiculo(73), b, n, 250).faixa, FaixaSonora::Rapida);
    EXPECT_EQ(m.avalia(veiculo(80), b, n, 500).faixa, FaixaSonora::Pulso);
}

TEST(MaquinaZona, faixa_sobe_na_hora_e_desce_com_dois_km_h_de_atraso) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(200, 0, 60)});
    const Ponto* b = base.data();
    const std::size_t n = base.size();

    ASSERT_EQ(m.avalia(veiculo(80), b, n, 0).faixa, FaixaSonora::Pulso);
    EXPECT_EQ(m.avalia(veiculo(77.5F), b, n, 250).faixa, FaixaSonora::Pulso);
    EXPECT_EQ(m.avalia(veiculo(77.0F), b, n, 500).faixa, FaixaSonora::Rapida);
    EXPECT_EQ(m.avalia(veiculo(71.0F), b, n, 750).faixa, FaixaSonora::Rapida);
    EXPECT_EQ(m.avalia(veiculo(70.0F), b, n, 1000).faixa, FaixaSonora::Lenta);
}

TEST(MaquinaZona, sair_da_zona_zera_a_faixa_para_a_proxima_aproximacao) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(200, 0, 60)});
    const Ponto* b = base.data();
    const std::size_t n = base.size();

    ASSERT_EQ(m.avalia(veiculo(80), b, n, 0).faixa, FaixaSonora::Pulso);
    ASSERT_EQ(m.avalia(veiculo(80), nullptr, 0, 250).zona, Zona::Segura);
    // 71 km/h cai **dentro** da janela de histerese da faixa Rápida (limiar
    // 72.6, rebaixado para 70.6). Sem o reset herdaria Rápida; com ele, o
    // limiar cheio vale de novo e a faixa é Lenta. Uma velocidade fora da
    // janela daria o mesmo resultado nos dois casos e não provaria nada.
    EXPECT_EQ(m.avalia(veiculo(71), b, n, 500).faixa, FaixaSonora::Lenta);
}

TEST(MaquinaZona, sair_da_zona_zera_tambem_a_histerese_de_perigo) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(200, 0, 60)});  // V_infra = 66
    const Ponto* b = base.data();
    const std::size_t n = base.size();

    ASSERT_EQ(m.avalia(veiculo(80), b, n, 0).zona, Zona::Perigo);
    ASSERT_EQ(m.avalia(veiculo(80), nullptr, 0, 250).zona, Zona::Segura);
    // 65 km/h está na janela: com Perigo pendurado passaria de 64, sem ele
    // não passa de 66.
    EXPECT_EQ(m.avalia(veiculo(65), b, n, 500).zona, Zona::AproximacaoMargem);
}

// ================================ RF03.4 precedência entre quatro candidatos

TEST(MaquinaZona, o_mais_grave_vence_o_mais_proximo) {
    // O cenário exato do requisito: o radar perto é inofensivo na velocidade
    // atual, o distante está multando. Mostrar o mais próximo esconderia a
    // multa em curso. O estado é por ponto, não global.
    MaquinaZona m;
    const auto base = base_de({
        faz_ponto(100, 0, 80),  // 70 ≤ 80 → conforme
        faz_ponto(250, 0, 60),  // 70 > 66 → perigo
    });
    const Veredito v = m.avalia(veiculo(70), base.data(), base.size(), 0);
    EXPECT_EQ(v.zona, Zona::Perigo);
    EXPECT_EQ(v.alvo.limite, 60);
    EXPECT_NEAR(v.distancia_m, 250.0F, 1.0F);
}

TEST(MaquinaZona, margem_vence_semaforo) {
    MaquinaZona m;
    const auto base = base_de({
        faz_ponto(50, 0, kSemLimite),
        faz_ponto(250, 0, 60),  // 63 está entre 60 e 66
    });
    const Veredito v = m.avalia(veiculo(63), base.data(), base.size(), 0);
    EXPECT_EQ(v.zona, Zona::AproximacaoMargem);
    EXPECT_EQ(v.alvo.limite, 60);
}

TEST(MaquinaZona, semaforo_vence_conforme) {
    MaquinaZona m;
    const auto base = base_de({
        faz_ponto(50, 0, 80),  // 55 ≤ 80 → conforme
        faz_ponto(250, 0, kSemLimite),
    });
    const Veredito v = m.avalia(veiculo(55), base.data(), base.size(), 0);
    EXPECT_EQ(v.zona, Zona::Semaforo);
    EXPECT_NEAR(v.distancia_m, 250.0F, 1.0F);
}

TEST(MaquinaZona, dentro_da_mesma_categoria_vence_o_mais_proximo) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(120, 0, 60), faz_ponto(250, 0, 60)});
    const Veredito v = m.avalia(veiculo(90), base.data(), base.size(), 0);
    EXPECT_EQ(v.zona, Zona::Perigo);
    EXPECT_NEAR(v.distancia_m, 120.0F, 1.0F);
}

TEST(MaquinaZona, a_faixa_sonora_usa_o_v_infra_do_alvo_escolhido) {
    // Escolhido o radar de 60 a 250 m, a faixa tem de sair do V_infra dele
    // (66), não do limite do radar próximo.
    MaquinaZona m;
    const auto base = base_de({faz_ponto(100, 0, 80), faz_ponto(250, 0, 60)});
    const Veredito v = m.avalia(veiculo(80), base.data(), base.size(), 0);
    ASSERT_EQ(v.alvo.limite, 60);
    EXPECT_NEAR(v.v_infra_kmh, 66.0F, 0.01F);
    EXPECT_EQ(v.faixa, FaixaSonora::Pulso);  // 80 > 79.2
}

// ================================================ RF02.1 cortes da varredura

TEST(MaquinaZona, ponto_longe_em_longitude_e_descartado_sem_calcular_distancia) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(0, 1000, 60)});
    EXPECT_EQ(m.avalia(veiculo(80), base.data(), base.size(), 0).zona,
              Zona::Segura);
}

TEST(MaquinaZona, acha_o_ponto_certo_no_meio_de_uma_base_grande) {
    std::vector<Ponto> pontos;
    for (int i = 0; i < 2000; ++i) {
        pontos.push_back(faz_ponto(static_cast<float>(i) * 500.0F, 0, 80));
    }
    pontos.push_back(faz_ponto(200.0F, 0, 60));  // o único a ≤ 300 m
    const auto base = base_de(pontos);

    MaquinaZona m;
    const Veredito v = m.avalia(veiculo(70), base.data(), base.size(), 0);
    EXPECT_EQ(v.zona, Zona::Perigo);
    EXPECT_EQ(v.alvo.limite, 60);
}

TEST(MaquinaZona, sentido_incompativel_nao_gera_alerta) {
    MaquinaZona m;
    const auto base =
        base_de({faz_ponto(200, 0, 60, Sentido::Unidirecional, 180)});
    EXPECT_EQ(m.avalia(veiculo(90, 0.0F), base.data(), base.size(), 0).zona,
              Zona::Segura);
}

TEST(MaquinaZona, sentido_incompativel_volta_a_valer_parado) {
    MaquinaZona m;
    const auto base =
        base_de({faz_ponto(200, 0, 60, Sentido::Unidirecional, 180)});
    EXPECT_TRUE(
        m.avalia(veiculo(3.0F, 0.0F), base.data(), base.size(), 0).tem_alvo);
}

// ============================================== §4.1 perda de sinal e alvo

TEST(MaquinaZona, sem_fix_suspende_o_alerta_de_imediato) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(200, 0, 60)});
    ASSERT_EQ(m.avalia(veiculo(90), base.data(), base.size(), 1000).zona,
              Zona::Perigo);

    const Veredito v = m.sem_fix(1250);
    EXPECT_EQ(v.zona, Zona::SemSinal);
    EXPECT_FALSE(v.tem_alvo);
    EXPECT_EQ(v.faixa, FaixaSonora::Nenhuma);
}

TEST(MaquinaZona, alvo_sobrevive_a_um_viaduto_de_nove_segundos) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(0, 0, 60)});
    ASSERT_TRUE(m.avalia(veiculo(55, 0.0F, -250.0F), base.data(), base.size(),
                         1000)
                    .tem_alvo);

    m.sem_fix(10000);  // 9 s sem sinal

    // Voltou o sinal com o veículo a 320 m: só continua em alerta se o alvo
    // tiver sobrevivido, porque 320 m está acima do raio de entrada.
    EXPECT_TRUE(m.avalia(veiculo(55, 0.0F, -320.0F), base.data(), base.size(),
                         10000)
                    .tem_alvo);
}

TEST(MaquinaZona, alvo_e_descartado_depois_de_dez_segundos_sem_fix) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(0, 0, 60)});
    ASSERT_TRUE(m.avalia(veiculo(55, 0.0F, -250.0F), base.data(), base.size(),
                         1000)
                    .tem_alvo);

    m.sem_fix(11000);  // 10 s exatos

    EXPECT_EQ(m.avalia(veiculo(55, 0.0F, -320.0F), base.data(), base.size(),
                       11000)
                  .zona,
              Zona::Segura);
}

TEST(MaquinaZona, o_descarte_atravessa_o_estouro_do_relogio) {
    // `to_ms_since_boot` estoura em 49 dias. Subtração em unsigned resolve
    // sozinha — desde que ninguém troque por uma comparação de grandeza.
    MaquinaZona m;
    const auto base = base_de({faz_ponto(0, 0, 60)});
    constexpr std::uint32_t kQuaseEstouro = 0xFFFFFF00U;
    ASSERT_TRUE(m.avalia(veiculo(55, 0.0F, -250.0F), base.data(), base.size(),
                         kQuaseEstouro)
                    .tem_alvo);

    m.sem_fix(static_cast<std::uint32_t>(kQuaseEstouro + 5000U));  // 5 s
    EXPECT_TRUE(m.avalia(veiculo(55, 0.0F, -320.0F), base.data(), base.size(),
                         static_cast<std::uint32_t>(kQuaseEstouro + 5000U))
                    .tem_alvo);

    m.sem_fix(static_cast<std::uint32_t>(kQuaseEstouro + 20000U));  // 15 s
    EXPECT_EQ(m.avalia(veiculo(55, 0.0F, -320.0F), base.data(), base.size(),
                       static_cast<std::uint32_t>(kQuaseEstouro + 20000U))
                  .zona,
              Zona::Segura);
}

TEST(MaquinaZona, sem_fix_antes_de_qualquer_fix_nao_quebra) {
    MaquinaZona m;
    EXPECT_EQ(m.sem_fix(0).zona, Zona::SemSinal);
    EXPECT_EQ(m.sem_fix(999999).zona, Zona::SemSinal);
}

// ====================================================== troca de base / reset

TEST(MaquinaZona, reinicia_esquece_o_alvo) {
    MaquinaZona m;
    const auto base = base_de({faz_ponto(0, 0, 60)});
    ASSERT_TRUE(m.avalia(veiculo(55, 0.0F, -250.0F), base.data(), base.size(), 0)
                    .tem_alvo);
    m.reinicia();
    EXPECT_EQ(
        m.avalia(veiculo(55, 0.0F, -320.0F), base.data(), base.size(), 250).zona,
        Zona::Segura);
}

TEST(MaquinaZona, base_recarregada_nao_ressuscita_outro_ponto_no_mesmo_indice) {
    // O perigo não é ler fora do array — é um ponto **diferente** cair no
    // índice do alvo antigo e herdar a retenção de 340 m que nunca conquistou.
    // Só a comparação de coordenadas separa os dois.
    MaquinaZona m;
    const auto antiga = base_de({faz_ponto(250, 0, 60)});
    ASSERT_TRUE(m.avalia(veiculo(90), antiga.data(), antiga.size(), 0).tem_alvo);

    const auto nova = base_de({faz_ponto(320, 0, 60)});  // outro radar, índice 0
    EXPECT_EQ(m.avalia(veiculo(90), nova.data(), nova.size(), 250).zona,
              Zona::Segura);
}

TEST(MaquinaZona, base_encolhida_nao_deixa_indice_pendurado) {
    MaquinaZona m;
    const auto grande = base_de({faz_ponto(100, 0, 60), faz_ponto(200, 0, 60),
                                 faz_ponto(250, 0, 60)});
    ASSERT_TRUE(m.avalia(veiculo(90), grande.data(), grande.size(), 0).tem_alvo);

    const auto pequena = base_de({faz_ponto(320, 0, 60)});
    EXPECT_EQ(m.avalia(veiculo(90), pequena.data(), pequena.size(), 250).zona,
              Zona::Segura);
}

// ================================================================ descrições

TEST(Zonamento, descricoes_cobrem_todos_os_estados) {
    for (const Zona z : {Zona::SemSinal, Zona::Segura, Zona::AproximacaoConforme,
                         Zona::Semaforo, Zona::AproximacaoMargem,
                         Zona::Perigo}) {
        EXPECT_STRNE(descreve(z), "?");
    }
    for (const FaixaSonora f : {FaixaSonora::Nenhuma, FaixaSonora::Lenta,
                                FaixaSonora::Rapida, FaixaSonora::Pulso}) {
        EXPECT_STRNE(descreve(f), "?");
    }
}


// --- o candidato mais proximo, que nao e o alvo ---
//
// O alvo vence por GRAVIDADE (RF03.4). Quem registra infracao consumada
// precisa saber de qual radar passou perto, e essa e outra pergunta.

TEST(MaquinaZona, o_mais_proximo_nao_e_o_alvo_quando_o_distante_e_mais_grave) {
    // O cenario que a propria precedencia do RF03.4 descreve: dois radares
    // na janela, o DISTANTE em Perigo e o PROXIMO conforme. O alvo tem de
    // ser o distante, e o mais_proximo tem de ser o outro.
    // Ordenada por latitude, como a busca binaria exige. Indo para o norte,
    // o de 80 fica a ~111 m e o de 60 a ~223 m -- os dois dentro dos 300 m.
    const Ponto base[] = {
        {-19.80000F, -44.00000F,  80, 0, TipoPonto::RadarFixo, Sentido::Omnidirecional},
        {-19.79900F, -44.00000F,  60, 0, TipoPonto::RadarFixo, Sentido::Omnidirecional},
    };
    Telemetria t;
    t.lat = -19.80100F;
    t.lon = -44.00000F;
    // V_infra(60) = 66 e V_infra(80) = 86, com desconto de 6 km/h.
    // A 70: conforme no de 80, e PERIGO no de 60, que esta mais longe.
    t.velocidade_kmh = 70.0F;
    t.rumo_graus = 0.0F;        // indo para o norte, os dois a frente
    t.rumo_valido = true;

    MaquinaZona m;
    const auto v = m.avalia(t, base, 2, 0);

    ASSERT_TRUE(v.tem_alvo);
    ASSERT_TRUE(v.tem_mais_proximo);
    EXPECT_EQ(v.zona, Zona::Perigo);
    EXPECT_EQ(v.alvo.limite, 60) << "o alvo tem de ser o DISTANTE, que e o grave";
    EXPECT_EQ(v.mais_proximo.limite, 80) << "o mais proximo e o outro";
    EXPECT_LT(v.dist_mais_proximo_m, v.distancia_m)
        << "por definicao o mais proximo esta mais perto que o alvo";
}

TEST(MaquinaZona, com_um_ponto_so_o_mais_proximo_e_o_alvo) {
    const Ponto base[] = {
        {-19.79900F, -44.00000F, 60, 0, TipoPonto::RadarFixo, Sentido::Omnidirecional},
    };
    Telemetria t;
    t.lat = -19.80000F;
    t.lon = -44.00000F;
    t.velocidade_kmh = 50.0F;
    t.rumo_graus = 0.0F;
    t.rumo_valido = true;

    MaquinaZona m;
    const auto v = m.avalia(t, base, 1, 0);

    ASSERT_TRUE(v.tem_mais_proximo);
    EXPECT_FLOAT_EQ(v.dist_mais_proximo_m, v.distancia_m);
    EXPECT_EQ(v.mais_proximo.limite, v.alvo.limite);
}

TEST(MaquinaZona, sem_candidato_nao_ha_mais_proximo) {
    const Ponto base[] = {
        {-10.00000F, -44.00000F, 60, 0, TipoPonto::RadarFixo, Sentido::Omnidirecional},
    };
    Telemetria t;
    t.lat = -19.80000F;
    t.lon = -44.00000F;
    t.velocidade_kmh = 50.0F;
    t.rumo_valido = true;

    MaquinaZona m;
    const auto v = m.avalia(t, base, 1, 0);

    EXPECT_FALSE(v.tem_alvo);
    EXPECT_FALSE(v.tem_mais_proximo);
}

}  // namespace
