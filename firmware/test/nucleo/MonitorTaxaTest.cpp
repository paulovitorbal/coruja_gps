#include "nucleo/MonitorTaxa.h"

#include <gtest/gtest.h>

namespace {

using namespace coruja;

constexpr std::uint32_t kPasso = 50;   // cadencia de avalia()

/// Avanca o relogio de `de` ate `ate`, registrando um fix a cada
/// `periodo_fix_ms` e chamando `avalia()` a cada passo -- como o firmware
/// faz. A sustentacao de 5 s do RF01.5 e contada entre chamadas, entao um
/// teste que so avalia no fim nao consegue observa-la.
///
/// `periodo_fix_ms` igual a zero significa silencio: so o relogio anda.
EstadoTaxa roda(MonitorTaxa* m, std::uint32_t de, std::uint32_t ate,
                std::uint32_t periodo_fix_ms) {
    EstadoTaxa e = m->estado();
    std::uint32_t proximo_fix = de;
    for (std::uint32_t t = de; t <= ate; t += kPasso) {
        if (periodo_fix_ms != 0 && t >= proximo_fix) {
            m->registra_fix(t);
            proximo_fix = t + periodo_fix_ms;
        }
        e = m->avalia(t);
    }
    return e;
}

/// Alimenta fixes sem avaliar, para os casos em que o instante importa.
void alimenta(MonitorTaxa* m, std::uint32_t de, std::uint32_t ate,
              std::uint32_t periodo_ms) {
    for (std::uint32_t t = de; t <= ate; t += periodo_ms) {
        m->registra_fix(t);
    }
}

// ================================================================ aquecimento

TEST(MonitorTaxa, comeca_aquecendo_e_nao_acusa_falha_no_boot) {
    // Sem aquecimento, uma taxa perfeita de 4 Hz mede 0,33 Hz no primeiro
    // quarto de segundo e o boot viraria alarme. Alarme que sempre dispara
    // no boot e' alarme que se aprende a ignorar.
    MonitorTaxa m;
    EXPECT_EQ(m.avalia(0), EstadoTaxa::Aquecendo);
    alimenta(&m, 0, 2750, 250);
    EXPECT_EQ(m.avalia(2999), EstadoTaxa::Aquecendo);
}

TEST(MonitorTaxa, sem_fix_nenhum_continua_aquecendo_e_nao_vira_falha) {
    MonitorTaxa m;
    EXPECT_EQ(m.avalia(100000), EstadoTaxa::Aquecendo);
    EXPECT_FALSE(m.precisa_reconfigurar());
}

// ================================================== classificacao do RF01.5

TEST(MonitorTaxa, quatro_hz_e_nominal) {
    MonitorTaxa m;
    alimenta(&m, 0, 3000, 250);
    EXPECT_EQ(m.avalia(3000), EstadoTaxa::Nominal);
    EXPECT_NEAR(m.taxa_hz(), 4.0F, 0.01F);
}

TEST(MonitorTaxa, tres_e_um_terco_de_hz_e_degradado) {
    MonitorTaxa m;
    alimenta(&m, 0, 3000, 300);           // 10 amostras na janela
    EXPECT_EQ(m.avalia(3000), EstadoTaxa::Degradado);
    EXPECT_NEAR(m.taxa_hz(), 3.333F, 0.01F);
}

TEST(MonitorTaxa, a_fronteira_entre_nominal_e_degradado) {
    // 3,5 Hz sao 10,5 amostras na janela de 3 s, e meia amostra nao existe:
    // a fronteira cai entre 10 (3,333 -> degradado) e 11 (3,667 -> nominal).
    // O fix em t=0 existe so para vencer o aquecimento; ele e podado.
    MonitorTaxa a;
    a.registra_fix(0);
    alimenta(&a, 500, 3000, 250);          // 11 amostras dentro da janela
    EXPECT_EQ(a.avalia(3000), EstadoTaxa::Nominal);
    EXPECT_NEAR(a.taxa_hz(), 3.667F, 0.01F);

    MonitorTaxa b;
    b.registra_fix(0);
    alimenta(&b, 750, 3000, 250);          // 10 amostras
    EXPECT_EQ(b.avalia(3000), EstadoTaxa::Degradado);
    EXPECT_NEAR(b.taxa_hz(), 3.333F, 0.01F);
}

TEST(MonitorTaxa, abaixo_do_piso_nao_e_falha_antes_de_cinco_segundos) {
    MonitorTaxa m;
    ASSERT_EQ(roda(&m, 0, 3000, 500), EstadoTaxa::Degradado);   // 2 Hz
    EXPECT_EQ(roda(&m, 3050, 7950, 500), EstadoTaxa::Degradado);
    EXPECT_FALSE(m.precisa_reconfigurar());
}

TEST(MonitorTaxa, abaixo_do_piso_sustentado_por_cinco_segundos_e_falha) {
    MonitorTaxa m;
    ASSERT_EQ(roda(&m, 0, 3000, 500), EstadoTaxa::Degradado);
    EXPECT_EQ(roda(&m, 3050, 8050, 500), EstadoTaxa::Falha);
}

TEST(MonitorTaxa, um_vale_curto_nao_derruba_para_falha) {
    // Tunel de 2 s: a taxa despenca e volta. Sem a exigencia de sustentacao,
    // todo viaduto reenviaria a configuracao UBX.
    MonitorTaxa m;
    ASSERT_EQ(roda(&m, 0, 3000, 250), EstadoTaxa::Nominal);
    EXPECT_EQ(roda(&m, 3050, 5000, 0), EstadoTaxa::Degradado);  // 2 s de silencio
    EXPECT_EQ(roda(&m, 5050, 8050, 250), EstadoTaxa::Nominal);
    EXPECT_FALSE(m.precisa_reconfigurar());
}

TEST(MonitorTaxa, recuperar_zera_a_contagem_de_sustentacao) {
    // Depois de voltar ao normal, uma nova queda precisa de 5 s NOVOS. Sem
    // isso, duas quedas curtas separadas por minutos somariam.
    MonitorTaxa m;
    ASSERT_EQ(roda(&m, 0, 4000, 500), EstadoTaxa::Degradado);   // abaixo do piso
    ASSERT_EQ(roda(&m, 4050, 7050, 250), EstadoTaxa::Nominal);  // recuperou
    // Cai de novo por 4 s: menos que os 5 de sustentacao. Se a contagem nao
    // tivesse reiniciado, os 4 s de antes somariam e isto seria falha.
    EXPECT_EQ(roda(&m, 7100, 11000, 500), EstadoTaxa::Degradado)
        << "a sustentacao deveria ter reiniciado ao recuperar";
}

TEST(MonitorTaxa, salto_direto_de_abaixo_do_piso_para_nominal_tambem_zera) {
    // A recuperacao normal sobe passando pela faixa degradada, que ja zera a
    // sustentacao. Mas a taxa e quantizada por amostras e pode SALTAR: uma
    // rajada que entra de uma vez leva a janela de 2 Hz a 5 Hz entre duas
    // avaliacoes, sem nunca medir um valor intermediario. Sem zerar tambem no
    // ramo nominal, a sustentacao antiga sobrevive a recuperacao e a proxima
    // queda vira falha instantanea.
    MonitorTaxa m;
    ASSERT_EQ(roda(&m, 0, 6000, 500), EstadoTaxa::Degradado);   // 2 Hz, 3 s abaixo

    for (std::uint32_t t = 5550; t <= 6000; t += 45) { m.registra_fix(t); }
    ASSERT_EQ(m.avalia(6000), EstadoTaxa::Nominal)
        << "a rajada deveria saltar direto para nominal";

    // Silencio. A janela so cruza o piso perto de 8,7 s (a rajada a mantem
    // cheia ate la), e a falha cai 5 s depois. Em 11 s ainda NAO e falha; em
    // 15 s ja e. Sem o zeramento, a sustentacao antiga faria a primeira
    // verificacao acusar falha.
    EXPECT_EQ(roda(&m, 6050, 11000, 0), EstadoTaxa::Degradado)
        << "a sustentacao antiga sobreviveu a recuperacao";
    EXPECT_EQ(roda(&m, 11050, 15000, 0), EstadoTaxa::Falha);
}

// ========================================== reconfiguracao uma vez por falha

TEST(MonitorTaxa, a_falha_arma_a_reconfiguracao_uma_unica_vez) {
    MonitorTaxa m;
    ASSERT_EQ(roda(&m, 0, 8050, 500), EstadoTaxa::Falha);
    EXPECT_TRUE(m.precisa_reconfigurar());

    m.reconfiguracao_enviada();
    EXPECT_FALSE(m.precisa_reconfigurar());

    // Seguir em falha NAO pode rearmar: reenviaria a sequencia UBX quatro
    // vezes por segundo contra um modulo que ja nao responde.
    ASSERT_EQ(roda(&m, 8100, 12000, 500), EstadoTaxa::Falha);
    EXPECT_FALSE(m.precisa_reconfigurar());
}

TEST(MonitorTaxa, uma_falha_nova_depois_de_recuperar_arma_de_novo) {
    MonitorTaxa m;
    ASSERT_EQ(roda(&m, 0, 8050, 500), EstadoTaxa::Falha);
    m.reconfiguracao_enviada();
    ASSERT_EQ(roda(&m, 8100, 11100, 250), EstadoTaxa::Nominal);
    // Precisa de mais de 5 s: a janela ainda carrega as amostras de 4 Hz por
    // 3 s depois da queda, e so entao a taxa cruza o piso e a contagem comeca.
    ASSERT_EQ(roda(&m, 11150, 20000, 500), EstadoTaxa::Falha);
    EXPECT_TRUE(m.precisa_reconfigurar())
        << "uma falha nova merece uma tentativa nova";
}

// ================================================= checksums, contados a parte

TEST(MonitorTaxa, checksum_invalido_conta_a_parte_e_nao_entra_na_taxa) {
    // Esta separacao e o coracao do diagnostico do RF01.5: "o GPS nao esta
    // enviando" e "estamos falhando em interpretar" dao o mesmo sintoma e
    // pedem correcoes opostas.
    MonitorTaxa m;
    alimenta(&m, 0, 3000, 250);
    for (int i = 0; i < 7; ++i) { m.registra_checksum_invalido(); }
    EXPECT_EQ(m.avalia(3000), EstadoTaxa::Nominal);
    EXPECT_NEAR(m.taxa_hz(), 4.0F, 0.01F);
    EXPECT_EQ(m.checksums_invalidos(), 7U);
}

TEST(MonitorTaxa, so_checksum_invalido_derruba_a_taxa_a_zero) {
    MonitorTaxa m;
    m.registra_fix(0);
    for (int i = 0; i < 50; ++i) { m.registra_checksum_invalido(); }
    EXPECT_EQ(roda(&m, 0, 9000, 0), EstadoTaxa::Falha);
    EXPECT_FLOAT_EQ(m.taxa_hz(), 0.0F);
    EXPECT_EQ(m.checksums_invalidos(), 50U);
}

// ======================================================= relogio e capacidade

TEST(MonitorTaxa, a_janela_atravessa_o_estouro_do_relogio) {
    constexpr std::uint32_t kQuase = 0xFFFFFF00U;
    MonitorTaxa m;
    for (std::uint32_t i = 0; i <= 12; ++i) {
        m.registra_fix(static_cast<std::uint32_t>(kQuase + i * 250U));
    }
    EXPECT_EQ(m.avalia(static_cast<std::uint32_t>(kQuase + 3000U)),
              EstadoTaxa::Nominal);
    EXPECT_NEAR(m.taxa_hz(), 4.0F, 0.01F);
}

TEST(MonitorTaxa, rajada_acima_da_capacidade_nao_corrompe_a_janela) {
    // Enchendo o anel, o mais antigo sai. Isso subestimaria a taxa, mas so a
    // partir de 10,7 Hz -- onde o veredito e nominal de todo jeito.
    MonitorTaxa m;
    for (std::uint32_t i = 0; i < 200; ++i) { m.registra_fix(i); }
    EXPECT_EQ(m.avalia(3000), EstadoTaxa::Nominal);
    EXPECT_LE(m.taxa_hz(), static_cast<float>(kCapacidadeJanela) / 3.0F + 0.01F);
}

TEST(MonitorTaxa, reinicia_zera_tudo) {
    MonitorTaxa m;
    ASSERT_EQ(roda(&m, 0, 8050, 500), EstadoTaxa::Falha);
    m.registra_checksum_invalido();

    m.reinicia();
    EXPECT_EQ(m.avalia(8100), EstadoTaxa::Aquecendo);
    EXPECT_EQ(m.checksums_invalidos(), 0U);
    EXPECT_FALSE(m.precisa_reconfigurar());
    EXPECT_FLOAT_EQ(m.taxa_hz(), 0.0F);
}

TEST(MonitorTaxa, descricoes_cobrem_todos_os_estados) {
    for (const EstadoTaxa e : {EstadoTaxa::Aquecendo, EstadoTaxa::Nominal,
                               EstadoTaxa::Degradado, EstadoTaxa::Falha}) {
        EXPECT_STRNE(descreve(e), "?");
    }
}

}  // namespace
