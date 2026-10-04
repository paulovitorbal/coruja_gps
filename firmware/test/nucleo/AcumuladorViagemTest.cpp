#include "nucleo/AcumuladorViagem.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

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
    EXPECT_EQ(a.alimenta(em(17, 30, 0, 60.0F), true, 1000), EventoViagem::Nada);
}

TEST(AcumuladorViagem, iniciar_sem_fix_fica_aguardando) {
    AcumuladorViagem a;
    a.inicia();
    EXPECT_EQ(a.estado(), EstadoViagem::Aguardando);
    EXPECT_EQ(a.alimenta(Telemetria{}, false, 1000), EventoViagem::Nada);
    EXPECT_EQ(a.estado(), EstadoViagem::Aguardando);
}

TEST(AcumuladorViagem, o_primeiro_fix_abre_o_arquivo_com_nome_em_utc) {
    AcumuladorViagem a;
    a.inicia();
    EXPECT_EQ(a.alimenta(em(17, 30, 42, 50.0F), true, 1000), EventoViagem::Abre);
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
    EXPECT_EQ(a.alimenta(t, true, 1000), EventoViagem::Nada);
    EXPECT_EQ(a.estado(), EstadoViagem::Aguardando);
}

TEST(AcumuladorViagem, parar_volta_a_parada) {
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 50.0F), true, 1000);
    a.para();
    EXPECT_EQ(a.estado(), EstadoViagem::Parada);
    EXPECT_EQ(a.alimenta(em(17, 31, 0, 50.0F), true, 61000), EventoViagem::Nada);
}

// --- o ponto por minuto ---

TEST(AcumuladorViagem, grava_na_virada_do_minuto_com_o_carimbo_do_minuto_descrito) {
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 10, 60.0F), true, 1000);        // abre
    EXPECT_EQ(a.alimenta(em(17, 30, 40, 60.0F), true, 31000), EventoViagem::Nada);
    ASSERT_EQ(a.alimenta(em(17, 31, 2, 60.0F), true, 53000), EventoViagem::Grava);

    const auto& p = a.ultimo_ponto();
    EXPECT_EQ(p.hora, 17);
    EXPECT_EQ(p.minuto, 30) << "a linha descreve o minuto que FECHOU";
    EXPECT_EQ(p.ano, 2026);
    EXPECT_EQ(p.mes, 10);
    EXPECT_EQ(p.dia, 4);
}

TEST(AcumuladorViagem, a_media_e_das_amostras_do_minuto) {
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 40.0F), true, 1000);
    a.alimenta(em(17, 30, 20, 60.0F), true, 21000);
    a.alimenta(em(17, 30, 40, 80.0F), true, 41000);
    ASSERT_EQ(a.alimenta(em(17, 31, 0, 10.0F), true, 61000), EventoViagem::Grava);

    EXPECT_FLOAT_EQ(a.ultimo_ponto().v_media_kmh, 60.0F)
        << "(40+60+80)/3; o 10 pertence ao minuto seguinte";
}

TEST(AcumuladorViagem, a_posicao_do_ponto_e_a_do_FIM_do_minuto) {
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 60.0F, -19.800F, -44.000F), true, 1000);
    a.alimenta(em(17, 30, 50, 60.0F, -19.810F, -44.020F), true, 51000);
    ASSERT_EQ(a.alimenta(em(17, 31, 0, 60.0F, -19.900F, -44.100F), true, 61000),
              EventoViagem::Grava);

    EXPECT_FLOAT_EQ(a.ultimo_ponto().lat, -19.810F);
    EXPECT_FLOAT_EQ(a.ultimo_ponto().lon, -44.020F);
}

TEST(AcumuladorViagem, minuto_inteiro_sem_fix_nao_produz_linha) {
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 60.0F), true, 1000);
    // Tunel: nada chega durante o minuto 31.
    for (std::uint32_t ms = 61000; ms < 121000; ms += 1000) {
        EXPECT_EQ(a.alimenta(Telemetria{}, false, ms), EventoViagem::Nada);
    }
    // Volta no minuto 32: fecha o 30, que tinha amostras. O 31 nao sai.
    ASSERT_EQ(a.alimenta(em(17, 32, 5, 60.0F), true, 125000), EventoViagem::Grava);
    EXPECT_EQ(a.ultimo_ponto().minuto, 30);
}

// --- distancia ---

TEST(AcumuladorViagem, a_distancia_vem_da_integracao_da_velocidade) {
    // 72 km/h durante 60 s = 1,2 km. Referencia calculada fora do codigo.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 72.0F), true, 0);
    for (std::uint32_t ms = 250; ms <= 60000; ms += 250) {
        const auto s = static_cast<std::uint8_t>((ms / 1000) % 60);
        const auto m = static_cast<std::uint8_t>(30 + ms / 60000);
        a.alimenta(em(17, m, s, 72.0F), true, ms);
    }
    EXPECT_NEAR(a.dist_km(), 1.2F, 0.01F);
}

TEST(AcumuladorViagem, parado_nao_acumula_distancia) {
    // O motivo de integrar velocidade em vez de somar coordenadas: a
    // velocidade Doppler parada e essencialmente zero, e o jitter de posicao
    // nao e.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 0.0F, -19.8000F, -44.0000F), true, 0);
    for (std::uint32_t ms = 250; ms <= 30000; ms += 250) {
        // Posicao tremendo, velocidade zero.
        const float jitter = (ms % 500 == 0) ? 0.00005F : -0.00005F;
        a.alimenta(em(17, 30, 0, 0.0F, -19.8F + jitter, -44.0F + jitter),
                   true, ms);
    }
    EXPECT_FLOAT_EQ(a.dist_km(), 0.0F);
}

TEST(AcumuladorViagem, o_buraco_de_fix_nao_vira_distancia_fantasma) {
    // Tunel de 5 minutos a 100 km/h: integrar o passo inteiro somaria 8,3 km
    // de uma tacada. O teto de 2 s corta isso.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 100.0F), true, 0);
    EXPECT_FLOAT_EQ(a.dist_km(), 0.0F);
    a.alimenta(em(17, 35, 0, 100.0F), true, 300000);
    EXPECT_FLOAT_EQ(a.dist_km(), 0.0F) << "somou o buraco inteiro";
}

TEST(AcumuladorViagem, buraco_CURTO_de_fix_ainda_integra) {
    // Contrapartida do teste acima, e e o que define o teto como o unico
    // mecanismo. Um segundo sem sentenca -- com velocidade conhecida antes e
    // depois -- estima-se bem por interpolacao; despreza-lo subcontaria a
    // viagem toda vez que uma sentenca se perdesse.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 72.0F), true, 0);
    a.alimenta(Telemetria{}, false, 500);                  // perdeu
    a.alimenta(em(17, 30, 1, 72.0F), true, 1000);          // voltou em 1 s

    // 72 km/h por 1 s = 20 m = 0,02 km.
    EXPECT_NEAR(a.dist_km(), 0.02F, 0.001F);
}

TEST(AcumuladorViagem, retoma_a_distancia_de_um_trecho_anterior) {
    AcumuladorViagem a;
    a.inicia(180.45F);
    EXPECT_FLOAT_EQ(a.dist_km(), 180.45F);
    a.alimenta(em(17, 30, 0, 72.0F), true, 0);
    a.alimenta(em(17, 30, 1, 72.0F), true, 1000);
    EXPECT_GT(a.dist_km(), 180.45F);
}

TEST(AcumuladorViagem, a_distancia_do_ponto_e_a_acumulada) {
    AcumuladorViagem a;
    a.inicia(10.0F);
    a.alimenta(em(17, 30, 0, 72.0F), true, 0);
    for (std::uint32_t ms = 250; ms <= 60000; ms += 250) {
        const auto s = static_cast<std::uint8_t>((ms / 1000) % 60);
        const auto m = static_cast<std::uint8_t>(30 + ms / 60000);
        a.alimenta(em(17, m, s, 72.0F), true, ms);
    }
    EXPECT_NEAR(a.ultimo_ponto().dist_km, 11.2F, 0.02F);
}

// --- encerramento automatico ---

TEST(AcumuladorViagem, cinco_minutos_parado_encerra_sozinha) {
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 0.0F), true, 0);
    EventoViagem ultimo = EventoViagem::Nada;
    for (std::uint32_t ms = 1000; ms <= kParadoEncerraViagemMs + 2000; ms += 1000) {
        const auto s = static_cast<std::uint8_t>((ms / 1000) % 60);
        const auto m = static_cast<std::uint8_t>(30 + ms / 60000);
        const auto e = a.alimenta(em(17, m, s, 0.0F), true, ms);
        if (e == EventoViagem::Encerra) { ultimo = e; break; }
    }
    EXPECT_EQ(ultimo, EventoViagem::Encerra);
    EXPECT_EQ(a.estado(), EstadoViagem::Parada);
}

TEST(AcumuladorViagem, andar_reinicia_a_contagem_de_parado) {
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 0.0F), true, 0);
    // Quatro minutos parado, um instante andando, quatro minutos parado.
    for (std::uint32_t ms = 1000; ms <= 240000; ms += 1000) {
        a.alimenta(em(17, 30, 0, 0.0F), true, ms);
    }
    a.alimenta(em(17, 34, 1, 20.0F), true, 241000);
    for (std::uint32_t ms = 242000; ms <= 480000; ms += 1000) {
        EXPECT_NE(a.alimenta(em(17, 38, 0, 0.0F), true, ms), EventoViagem::Encerra)
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
    a.alimenta(em(17, 30, 0, 0.0F), true, 0);
    a.alimenta(em(17, 30, 1, 0.0F), true, 1000);     // contando parado

    for (std::uint32_t ms = 2000; ms <= 400000; ms += 1000) {
        a.alimenta(Telemetria{}, false, ms);          // subsolo
    }

    EXPECT_NE(a.alimenta(em(17, 37, 0, 0.0F), true, 401000),
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
    a.alimenta(em(17, 30, 0, 0.0F), true, 0);        // abre
    a.alimenta(em(17, 30, 0, 0.0F), true, 1000);     // comeca a contar aqui

    EXPECT_NE(a.alimenta(em(17, 30, 0, 0.0F), true,
                         1000 + kParadoEncerraViagemMs - 1),
              EventoViagem::Encerra)
        << "encerrou um milissegundo antes do limiar";
    EXPECT_EQ(a.alimenta(em(17, 30, 0, 0.0F), true,
                         1000 + kParadoEncerraViagemMs),
              EventoViagem::Encerra)
        << "o limiar e inclusivo";
}

TEST(AcumuladorViagem, sem_fix_nao_conta_para_o_encerramento) {
    // Sem fix nao ha prova de estar parado. Contar o tunel como parada
    // encerraria a viagem no meio de uma estrada.
    AcumuladorViagem a;
    a.inicia();
    a.alimenta(em(17, 30, 0, 80.0F), true, 0);
    for (std::uint32_t ms = 1000; ms <= kParadoEncerraViagemMs + 60000; ms += 1000) {
        EXPECT_NE(a.alimenta(Telemetria{}, false, ms), EventoViagem::Encerra);
    }
    EXPECT_EQ(a.estado(), EstadoViagem::Gravando);
}

}  // namespace
