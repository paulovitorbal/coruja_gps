#include "nucleo/AcumuladorViagem.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

using namespace coruja;

/// Nenhum radar na janela. A esmagadora maioria destes casos nao fala de
/// radar nenhum -- eles medem fatia, distancia e encerramento --, e passar
/// isto explicitamente deixa visivel que a amostra agora CARREGA essa
/// informacao, em vez de escondê-la num parametro com valor padrao.
const RadarDaAmostra kSemRadar{};

Telemetria em(std::uint8_t hora, std::uint8_t minuto, std::uint8_t segundo,
              float velocidade_kmh, float lat = -19.8F, float lon = -44.0F) {
    Telemetria t;
    t.lat = lat;
    t.lon = lon;
    t.velocidade_kmh = velocidade_kmh;
    t.ano = 2026; t.mes = 10; t.dia = 4;
    t.hora = hora; t.minuto = minuto; t.segundo = segundo;
    t.data_valida = true;
    return t;
}

// --- a maquina de estados ---

TEST(AcumuladorViagem, nasce_parada_e_nao_grava_nada) {
    AcumuladorViagem a;
    EXPECT_EQ(a.estado(), EstadoViagem::Parada);
    EXPECT_EQ(a.alimenta(em(17, 30, 0, 60.0F), kSemRadar, true, 1000), EventoViagem::Nada);
}

TEST(AcumuladorViagem, iniciar_sem_fix_fica_aguardando) {
    AcumuladorViagem a;
    a.inicia();
    EXPECT_EQ(a.estado(), EstadoViagem::Aguardando);
    EXPECT_EQ(a.alimenta(Telemetria{}, kSemRadar, false, 1000), EventoViagem::Nada);
    EXPECT_EQ(a.estado(), EstadoViagem::Aguardando);
}

TEST(AcumuladorViagem, o_primeiro_fix_abre_o_arquivo_com_nome_em_utc) {
    AcumuladorViagem a;
    a.inicia();
    EXPECT_EQ(a.alimenta(em(17, 30, 42, 50.0F), kSemRadar, true, 1000), EventoViagem::Abre);
    EXPECT_STREQ(a.nome_arquivo(), "20261004_173042.log");
    EXPECT_EQ(a.estado(), EstadoViagem::Gravando);
}

TEST(AcumuladorViagem, fix_sem_data_valida_nao_abre) {
    // Sem data nao ha nome de arquivo. O fix de posicao pode chegar antes do
    // de tempo, e abrir com data zerada produziria 00010101_000000.log.
    AcumuladorViagem a;
    a.inicia();
    auto t = em(17, 30, 0, 50.0F);
    t.data_valida = false;
    EXPECT_EQ(a.alimenta(t, kSemRadar, true, 1000), EventoViagem::Nada);
    EXPECT_EQ(a.estado(), EstadoViagem::Aguardando);
}

TEST(AcumuladorViagem, parar_volta_a_parada) {
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 50.0F), kSemRadar, true, 1000);
    a.para();
    EXPECT_EQ(a.estado(), EstadoViagem::Parada);
    EXPECT_EQ(a.alimenta(em(17, 31, 0, 50.0F), kSemRadar, true, 61000), EventoViagem::Nada);
}

// --- a fatia de seis segundos ---
//
// Dez por minuto desde 2026-10-06. A conta que condenou a taxa anterior:
// 120 km/h sao 2 km por MINUTO, e um vertice a cada dois quilometros nao
// descreve trajeto -- numa via expressa vira uma reta entre pontos que
// ignoram as curvas e as alcas por onde o carro passou.

TEST(AcumuladorViagem, grava_na_virada_da_fatia_com_o_carimbo_da_fatia_descrita) {
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 6, 60.0F), kSemRadar, true, 1000);         // abre a fatia :06
    EXPECT_EQ(a.alimenta(em(17, 30, 10, 60.0F), kSemRadar, true, 5000), EventoViagem::Nada)
        << ":10 ainda esta na fatia que vai de :06 a :11";
    ASSERT_EQ(a.alimenta(em(17, 30, 12, 60.0F), kSemRadar, true, 7000), EventoViagem::Grava);

    const auto& p = a.ultimo_ponto();
    EXPECT_EQ(p.hora, 17);
    EXPECT_EQ(p.minuto, 30);
    EXPECT_EQ(p.segundo, 6) << "o carimbo e o INICIO da fatia que fechou";
    EXPECT_EQ(p.ano, 2026);
    EXPECT_EQ(p.mes, 10);
    EXPECT_EQ(p.dia, 4);
}

TEST(AcumuladorViagem, as_fatias_caem_em_segundos_REDONDOS) {
    // Seis divide sessenta, e e por isso que os carimbos sao :00, :06, :12...
    // Uma janela deslizante desde o clique daria carimbos que nao se alinham
    // entre viagens, e comparar dois arquivos viraria adivinhacao.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 3, 50.0F), kSemRadar, true, 1000);   // abre na fatia :00
    std::vector<int> carimbos;
    for (int s = 6; s < 60; s += 6) {
        if (a.alimenta(em(17, 30, s, 50.0F), kSemRadar, true, 1000U + s * 1000U)
                == EventoViagem::Grava) {
            carimbos.push_back(a.ultimo_ponto().segundo);
        }
    }
    EXPECT_EQ(carimbos, (std::vector<int>{0, 6, 12, 18, 24, 30, 36, 42, 48}));
}

TEST(AcumuladorViagem, dez_gravacoes_por_minuto) {
    // O numero que o autor pediu, afirmado diretamente.
    AcumuladorViagem a;
    a.inicia();
    int gravacoes = 0;
    // Um minuto inteiro, amostrando a 4 Hz como o GPS entrega.
    for (int ds = 0; ds < 240; ++ds) {
        const auto s = static_cast<std::uint8_t>(ds / 4);
        if (a.alimenta(em(17, 30, s, 50.0F), kSemRadar, true, 1000U + ds * 250U)
                == EventoViagem::Grava) {
            ++gravacoes;
        }
    }
    // Nove fechadas dentro do minuto; a decima fecha na virada para :31.
    EXPECT_EQ(gravacoes, 9);
    ASSERT_EQ(a.alimenta(em(17, 31, 0, 50.0F), kSemRadar, true, 61000),
              EventoViagem::Grava);
    EXPECT_EQ(a.ultimo_ponto().segundo, 54);
}

TEST(AcumuladorViagem, a_virada_do_minuto_fecha_a_fatia) {
    // A fatia :54 do minuto 30 e a :54 do minuto 31 tem o mesmo indice.
    // Comparar so o indice daria falso negativo e juntaria os dois minutos
    // numa linha so.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 54, 60.0F), kSemRadar, true, 1000);
    ASSERT_EQ(a.alimenta(em(17, 31, 56, 60.0F), kSemRadar, true, 63000),
              EventoViagem::Grava);
    EXPECT_EQ(a.ultimo_ponto().minuto, 30);
    EXPECT_EQ(a.ultimo_ponto().segundo, 54);
}

TEST(AcumuladorViagem, a_media_e_das_amostras_da_fatia) {
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 40.0F), kSemRadar, true, 1000);
    a.alimenta(em(17, 30, 2, 60.0F), kSemRadar, true, 3000);
    a.alimenta(em(17, 30, 4, 80.0F), kSemRadar, true, 5000);
    ASSERT_EQ(a.alimenta(em(17, 30, 6, 10.0F), kSemRadar, true, 7000), EventoViagem::Grava);

    EXPECT_FLOAT_EQ(a.ultimo_ponto().v_media_kmh, 60.0F)
        << "(40+60+80)/3; o 10 pertence a fatia seguinte";
}

TEST(AcumuladorViagem, a_posicao_do_ponto_e_a_do_FIM_da_fatia) {
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 60.0F, -19.800F, -44.000F), kSemRadar, true, 1000);
    a.alimenta(em(17, 30, 4, 60.0F, -19.810F, -44.020F), kSemRadar, true, 5000);
    ASSERT_EQ(a.alimenta(em(17, 30, 6, 60.0F, -19.900F, -44.100F), kSemRadar, true, 7000),
              EventoViagem::Grava);

    EXPECT_FLOAT_EQ(a.ultimo_ponto().lat, -19.810F);
    EXPECT_FLOAT_EQ(a.ultimo_ponto().lon, -44.020F);
}

TEST(AcumuladorViagem, fatia_inteira_sem_fix_nao_produz_linha) {
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 60.0F), kSemRadar, true, 1000);
    // Tunel: nada chega durante as fatias :06 e :12.
    for (std::uint32_t ms = 7000; ms < 19000; ms += 500) {
        EXPECT_EQ(a.alimenta(Telemetria{}, kSemRadar, false, ms), EventoViagem::Nada);
    }
    // Volta em :18: fecha a :00, que tinha amostras. As duas do meio nao saem.
    ASSERT_EQ(a.alimenta(em(17, 30, 18, 60.0F), kSemRadar, true, 19000),
              EventoViagem::Grava);
    EXPECT_EQ(a.ultimo_ponto().segundo, 0);
}

// --- distancia ---

TEST(AcumuladorViagem, a_distancia_vem_da_integracao_da_velocidade) {
    // 72 km/h durante 60 s = 1,2 km. Referencia calculada fora do codigo.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 72.0F), kSemRadar, true, 0);
    for (std::uint32_t ms = 250; ms <= 60000; ms += 250) {
        const auto s = static_cast<std::uint8_t>((ms / 1000) % 60);
        const auto m = static_cast<std::uint8_t>(30 + ms / 60000);
        a.alimenta(em(17, m, s, 72.0F), kSemRadar, true, ms);
    }
    EXPECT_NEAR(a.dist_km(), 1.2F, 0.01F);
}

TEST(AcumuladorViagem, parado_nao_acumula_distancia) {
    // O motivo de integrar velocidade em vez de somar coordenadas: a
    // velocidade Doppler parada e essencialmente zero, e o jitter de posicao
    // nao e.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 0.0F, -19.8000F, -44.0000F), kSemRadar, true, 0);
    for (std::uint32_t ms = 250; ms <= 30000; ms += 250) {
        // Posicao tremendo, velocidade zero.
        const float jitter = (ms % 500 == 0) ? 0.00005F : -0.00005F;
        a.alimenta(em(17, 30, 0, 0.0F, -19.8F + jitter, -44.0F + jitter),
                   kSemRadar, true, ms);
    }
    EXPECT_FLOAT_EQ(a.dist_km(), 0.0F);
}

TEST(AcumuladorViagem, o_buraco_de_fix_nao_vira_distancia_fantasma) {
    // Tunel de 5 minutos a 100 km/h: integrar o passo inteiro somaria 8,3 km
    // de uma tacada. O teto de 2 s corta isso.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 100.0F), kSemRadar, true, 0);
    EXPECT_FLOAT_EQ(a.dist_km(), 0.0F);
    a.alimenta(em(17, 35, 0, 100.0F), kSemRadar, true, 300000);
    EXPECT_FLOAT_EQ(a.dist_km(), 0.0F) << "somou o buraco inteiro";
}

TEST(AcumuladorViagem, buraco_CURTO_de_fix_ainda_integra) {
    // Contrapartida do teste acima, e e o que define o teto como o unico
    // mecanismo. Um segundo sem sentenca -- com velocidade conhecida antes e
    // depois -- estima-se bem por interpolacao; despreza-lo subcontaria a
    // viagem toda vez que uma sentenca se perdesse.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 72.0F), kSemRadar, true, 0);
    a.alimenta(Telemetria{}, kSemRadar, false, 500);                  // perdeu
    a.alimenta(em(17, 30, 1, 72.0F), kSemRadar, true, 1000);          // voltou em 1 s

    // 72 km/h por 1 s = 20 m = 0,02 km.
    EXPECT_NEAR(a.dist_km(), 0.02F, 0.001F);
}

TEST(AcumuladorViagem, retoma_a_distancia_de_um_trecho_anterior) {
    AcumuladorViagem a;
    a.inicia(180.45F);
    EXPECT_FLOAT_EQ(a.dist_km(), 180.45F);
    a.alimenta(em(17, 30, 0, 72.0F), kSemRadar, true, 0);
    a.alimenta(em(17, 30, 1, 72.0F), kSemRadar, true, 1000);
    EXPECT_GT(a.dist_km(), 180.45F);
}

TEST(AcumuladorViagem, a_distancia_do_ponto_e_a_acumulada) {
    AcumuladorViagem a;
    a.inicia(10.0F);
    a.alimenta(em(17, 30, 0, 72.0F), kSemRadar, true, 0);
    for (std::uint32_t ms = 250; ms <= 60000; ms += 250) {
        const auto s = static_cast<std::uint8_t>((ms / 1000) % 60);
        const auto m = static_cast<std::uint8_t>(30 + ms / 60000);
        a.alimenta(em(17, m, s, 72.0F), kSemRadar, true, ms);
    }
    EXPECT_NEAR(a.ultimo_ponto().dist_km, 11.2F, 0.02F);
}

// --- encerramento automatico ---

TEST(AcumuladorViagem, cinco_minutos_parado_encerra_sozinha) {
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 0.0F), kSemRadar, true, 0);
    EventoViagem ultimo = EventoViagem::Nada;
    for (std::uint32_t ms = 1000; ms <= kParadoEncerraViagemMs + 2000; ms += 1000) {
        const auto s = static_cast<std::uint8_t>((ms / 1000) % 60);
        const auto m = static_cast<std::uint8_t>(30 + ms / 60000);
        const auto e = a.alimenta(em(17, m, s, 0.0F), kSemRadar, true, ms);
        if (e == EventoViagem::Encerra) { ultimo = e; break; }
    }
    EXPECT_EQ(ultimo, EventoViagem::Encerra);
    EXPECT_EQ(a.estado(), EstadoViagem::Parada);
}

TEST(AcumuladorViagem, andar_reinicia_a_contagem_de_parado) {
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 0.0F), kSemRadar, true, 0);
    // Quatro minutos parado, um instante andando, quatro minutos parado.
    for (std::uint32_t ms = 1000; ms <= 240000; ms += 1000) {
        a.alimenta(em(17, 30, 0, 0.0F), kSemRadar, true, ms);
    }
    a.alimenta(em(17, 34, 1, 20.0F), kSemRadar, true, 241000);
    for (std::uint32_t ms = 242000; ms <= 480000; ms += 1000) {
        EXPECT_NE(a.alimenta(em(17, 38, 0, 0.0F), kSemRadar, true, ms), EventoViagem::Encerra)
            << "encerrou em " << ms << " ms, mas o relogio reiniciou aos 241 s";
    }
    EXPECT_EQ(a.estado(), EstadoViagem::Gravando);
}

TEST(AcumuladorViagem, parado_e_perdendo_o_fix_nao_encerra_ao_voltar) {
    // Garagem no subsolo: para, o contador comeca, o fix some. Se o contador
    // seguisse correndo no escuro, a viagem encerraria no instante em que o
    // sinal volta -- que e exatamente quando o carro esta saindo. Perder o
    // fix zera a contagem, porque sem fix nao ha prova de estar parado.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 0.0F), kSemRadar, true, 0);
    a.alimenta(em(17, 30, 1, 0.0F), kSemRadar, true, 1000);     // contando parado

    for (std::uint32_t ms = 2000; ms <= 400000; ms += 1000) {
        a.alimenta(Telemetria{}, kSemRadar, false, ms);          // subsolo
    }

    EXPECT_NE(a.alimenta(em(17, 37, 0, 0.0F), kSemRadar, true, 401000),
              EventoViagem::Encerra)
        << "encerrou assim que o fix voltou";
    EXPECT_EQ(a.estado(), EstadoViagem::Gravando);
}

TEST(AcumuladorViagem, o_limiar_de_encerramento_e_inclusivo) {
    // A fronteira exata. O relogio UTC fica parado de proposito: isola o
    // temporizador monotonico, que e o que esta sob teste, de uma virada de
    // minuto que devolveria `Grava` no meio do caminho.
    //
    // A contagem comeca na primeira amostra PARADA depois da abertura, nao
    // na abertura -- o ramo que abre o arquivo retorna antes.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 0.0F), kSemRadar, true, 0);        // abre
    a.alimenta(em(17, 30, 0, 0.0F), kSemRadar, true, 1000);     // comeca a contar aqui

    EXPECT_NE(a.alimenta(em(17, 30, 0, 0.0F), kSemRadar, true,
                         1000 + kParadoEncerraViagemMs - 1),
              EventoViagem::Encerra)
        << "encerrou um milissegundo antes do limiar";
    EXPECT_EQ(a.alimenta(em(17, 30, 0, 0.0F), kSemRadar, true,
                         1000 + kParadoEncerraViagemMs),
              EventoViagem::Encerra)
        << "o limiar e inclusivo";
}

TEST(AcumuladorViagem, sem_fix_nao_conta_para_o_encerramento) {
    // Sem fix nao ha prova de estar parado. Contar o tunel como parada
    // encerraria a viagem no meio de uma estrada.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 80.0F), kSemRadar, true, 0);
    for (std::uint32_t ms = 1000; ms <= kParadoEncerraViagemMs + 60000; ms += 1000) {
        EXPECT_NE(a.alimenta(Telemetria{}, kSemRadar, false, ms), EventoViagem::Encerra);
    }
    EXPECT_EQ(a.estado(), EstadoViagem::Gravando);
}


// --- o radar na amostra (Eixao, 07/10/2026) -------------------------------

/// Uma leitura com alerta e/ou ponto mais proximo.
RadarDaAmostra radar(float dist_alerta, unsigned limite_alerta,
                     float dist_proximo = -1.0F, unsigned limite_proximo = 0) {
    RadarDaAmostra r;
    if (dist_alerta >= 0.0F) {
        r.tem_alerta = true;
        r.dist_alerta_m = dist_alerta;
        r.limite_alerta = static_cast<std::uint8_t>(limite_alerta);
    }
    if (dist_proximo >= 0.0F) {
        r.tem_proximo = true;
        r.dist_proximo_m = dist_proximo;
        r.limite_proximo = static_cast<std::uint8_t>(limite_proximo);
    }
    return r;
}

TEST(RadarDaViagem, a_fatia_guarda_a_MENOR_distancia_e_nao_a_ultima) {
    // A 80 km/h o carro anda 133 m nos seis segundos da fatia. Guardar a
    // ultima leitura perderia o ponto de maior aproximacao justamente onde
    // ele interessa -- e e o numero que diz se o radar foi mesmo passado.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 80.0F), radar(250.0F, 60), true, 1000);
    a.alimenta(em(17, 30, 2, 80.0F), radar(120.0F, 60), true, 3000);
    a.alimenta(em(17, 30, 4, 80.0F), radar(180.0F, 60), true, 5000);
    ASSERT_EQ(a.alimenta(em(17, 30, 6, 80.0F), kSemRadar, true, 7000),
              EventoViagem::Grava);

    EXPECT_TRUE(a.ultimo_ponto().radar.tem_alerta);
    EXPECT_FLOAT_EQ(a.ultimo_ponto().radar.dist_alerta_m, 120.0F);
    EXPECT_EQ(a.ultimo_ponto().radar.limite_alerta, 60);
}

TEST(RadarDaViagem, o_limite_acompanha_a_leitura_mais_proxima) {
    // Nao basta guardar a menor distancia: o LIMITE tem de ser o do radar
    // daquela leitura. Guardar os dois de leituras diferentes produziria uma
    // linha que descreve um radar que nao existe.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 80.0F), radar(250.0F, 80), true, 1000);
    a.alimenta(em(17, 30, 2, 80.0F), radar(110.0F, 60), true, 3000);
    ASSERT_EQ(a.alimenta(em(17, 30, 6, 80.0F), kSemRadar, true, 7000),
              EventoViagem::Grava);

    EXPECT_FLOAT_EQ(a.ultimo_ponto().radar.dist_alerta_m, 110.0F);
    EXPECT_EQ(a.ultimo_ponto().radar.limite_alerta, 60)
        << "o limite veio de outra leitura que nao a mais proxima";
}

TEST(RadarDaViagem, o_caso_do_eixao_sai_com_os_dois_radares_distintos) {
    // O alerta vem do radar de 60 da pista lateral (venceu por gravidade);
    // o mais proximo e o de 80 da pista onde o carro de fato esta. E essa
    // divergencia que a coluna existe para mostrar.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 80.0F), radar(210.0F, 60, 88.0F, 80), true, 1000);
    ASSERT_EQ(a.alimenta(em(17, 30, 6, 80.0F), kSemRadar, true, 7000),
              EventoViagem::Grava);

    const auto& r = a.ultimo_ponto().radar;
    EXPECT_EQ(r.limite_alerta, 60);
    EXPECT_EQ(r.limite_proximo, 80);
    EXPECT_GT(r.dist_alerta_m, r.dist_proximo_m)
        << "o que foi alertado estava MAIS LONGE que o mais proximo";
}

TEST(RadarDaViagem, a_fatia_seguinte_comeca_do_zero) {
    // Sem limpar, o radar de uma fatia vazaria para a proxima e a linha diria
    // que havia radar onde nao havia.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 80.0F), radar(100.0F, 60), true, 1000);
    ASSERT_EQ(a.alimenta(em(17, 30, 6, 80.0F), kSemRadar, true, 7000),
              EventoViagem::Grava);
    ASSERT_TRUE(a.ultimo_ponto().radar.tem_alerta);

    a.alimenta(em(17, 30, 8, 80.0F), kSemRadar, true, 9000);
    ASSERT_EQ(a.alimenta(em(17, 30, 12, 80.0F), kSemRadar, true, 13000),
              EventoViagem::Grava);
    EXPECT_FALSE(a.ultimo_ponto().radar.tem_alerta)
        << "o radar da fatia anterior vazou para esta";
}

TEST(RadarDaViagem, a_leitura_da_virada_pertence_a_fatia_NOVA) {
    // A ordem que erra em silencio: a leitura que PROVOCA a virada e da fatia
    // que comeca, nao da que termina. Contabiliza-la na anterior atribuiria o
    // radar a seis segundos antes de ele ter sido visto.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 80.0F), kSemRadar, true, 1000);
    ASSERT_EQ(a.alimenta(em(17, 30, 6, 80.0F), radar(90.0F, 60), true, 7000),
              EventoViagem::Grava);
    EXPECT_FALSE(a.ultimo_ponto().radar.tem_alerta)
        << "a leitura da virada entrou na fatia que estava fechando";

    ASSERT_EQ(a.alimenta(em(17, 30, 12, 80.0F), kSemRadar, true, 13000),
              EventoViagem::Grava);
    EXPECT_TRUE(a.ultimo_ponto().radar.tem_alerta);
    EXPECT_FLOAT_EQ(a.ultimo_ponto().radar.dist_alerta_m, 90.0F);
}

TEST(RadarDaViagem, alerta_e_mais_proximo_sao_independentes) {
    // Pode haver ponto mais proximo sem alvo escolhido -- todos filtrados por
    // sentido, por exemplo. Um `if` so para os dois perderia metade.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 80.0F), radar(-1.0F, 0, 55.0F, 40), true, 1000);
    ASSERT_EQ(a.alimenta(em(17, 30, 6, 80.0F), kSemRadar, true, 7000),
              EventoViagem::Grava);

    EXPECT_FALSE(a.ultimo_ponto().radar.tem_alerta);
    EXPECT_TRUE(a.ultimo_ponto().radar.tem_proximo);
    EXPECT_FLOAT_EQ(a.ultimo_ponto().radar.dist_proximo_m, 55.0F);
}

}  // namespace
