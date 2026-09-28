#include "led/PadraoLed.h"

#include <gtest/gtest.h>

#include <set>

namespace {

using namespace coruja;

/// Cores vistas ao longo de uma janela, em passos de 10 ms.
std::set<std::uint32_t> vistas(PadraoLed* p, std::uint32_t de,
                               std::uint32_t ate) {
    std::set<std::uint32_t> s;
    for (std::uint32_t t = de; t <= ate; t += 10) {
        const Cor c = p->cor(t);
        s.insert((static_cast<std::uint32_t>(c.r) << 16) |
                 (static_cast<std::uint32_t>(c.g) << 8) | c.b);
    }
    return s;
}
std::uint32_t chave(const Cor& c) {
    return (static_cast<std::uint32_t>(c.r) << 16) |
           (static_cast<std::uint32_t>(c.g) << 8) | c.b;
}

TEST(PadraoLed, as_zonas_fixas_nao_piscam) {
    PadraoLed p;
    p.define_zona(Zona::Segura, 0);
    EXPECT_EQ(vistas(&p, 0, 3000).size(), 1U);
    EXPECT_EQ(p.cor(1234), cores::kVerde);

    PadraoLed q;
    q.define_zona(Zona::AproximacaoConforme, 0);
    EXPECT_EQ(vistas(&q, 0, 3000).size(), 1U);
    EXPECT_EQ(q.cor(1234), cores::kAmarelo);
}

TEST(PadraoLed, sem_sinal_fica_AZUL_e_nem_verde_nem_apagado) {
    // Verde diria "nao ha radar por perto", que e justamente o que nao se
    // sabe. E apagado significava duas coisas: "perdi o GPS" e "o aparelho
    // morreu". Com o azul, o escuro passa a ter um significado so.
    PadraoLed p;
    p.define_zona(Zona::SemSinal, 0);
    EXPECT_EQ(p.cor(0), cores::kAzul);
    EXPECT_EQ(p.cor(5000), cores::kAzul) << "nao pisca: sem fix nao ha acao";
}

TEST(PadraoLed, o_LED_apagado_passa_a_significar_SO_aparelho_sem_vida) {
    // Nenhuma zona acende apagado. E o que da ao escuro um unico
    // significado -- se um dia alguma voltar a usa-lo, esta ambiguidade
    // volta junto, e este teste avisa.
    for (const auto z : {Zona::SemSinal, Zona::Segura,
                         Zona::AproximacaoConforme, Zona::AproximacaoMargem,
                         Zona::Perigo, Zona::Semaforo}) {
        PadraoLed p;
        p.define_zona(z, 0);
        bool sempre_apagado = true;
        for (std::uint32_t t = 0; t <= 2000; t += 10) {
            if (p.cor(t) != cores::kApagado) { sempre_apagado = false; }
        }
        EXPECT_FALSE(sempre_apagado) << "zona " << descreve(z)
            << " deixa o LED morto, e morto tem de querer dizer sem energia";
    }
}

TEST(PadraoLed, margem_pisca_a_1_hz) {
    PadraoLed p;
    p.define_zona(Zona::AproximacaoMargem, 0);
    EXPECT_EQ(p.cor(0), cores::kRosa);
    EXPECT_EQ(p.cor(499), cores::kRosa);
    EXPECT_EQ(p.cor(500), cores::kApagado);
    EXPECT_EQ(p.cor(999), cores::kApagado);
    EXPECT_EQ(p.cor(1000), cores::kRosa);
}

TEST(PadraoLed, perigo_pisca_a_4_hz) {
    PadraoLed p;
    p.define_zona(Zona::Perigo, 0);
    EXPECT_EQ(p.cor(0), cores::kVermelho);
    EXPECT_EQ(p.cor(124), cores::kVermelho);
    EXPECT_EQ(p.cor(125), cores::kApagado);
    EXPECT_EQ(p.cor(250), cores::kVermelho);
}

TEST(PadraoLed, semaforo_alterna_duas_cores_a_2_hz_sem_apagar) {
    PadraoLed p;
    p.define_zona(Zona::Semaforo, 0);
    EXPECT_EQ(p.cor(0), cores::kAmarelo);
    EXPECT_EQ(p.cor(250), cores::kVermelho);
    EXPECT_EQ(p.cor(500), cores::kAmarelo);
    const auto s = vistas(&p, 0, 2000);
    EXPECT_EQ(s.size(), 2U);
    EXPECT_EQ(s.count(chave(cores::kApagado)), 0U);
}

TEST(PadraoLed, as_cinco_zonas_com_cor_sao_distinguiveis_entre_si) {
    // Duas zonas com a mesma cor tornariam o LED inutil como diagnostico.
    std::set<std::uint32_t> s;
    for (const auto z : {Zona::Segura, Zona::AproximacaoConforme,
                         Zona::AproximacaoMargem, Zona::Perigo,
                         Zona::SemSinal}) {
        PadraoLed p;
        p.define_zona(z, 0);
        s.insert(chave(p.cor(0)));
    }
    EXPECT_EQ(s.size(), 5U);
}

TEST(PadraoLed, o_rosa_nao_colide_com_os_vizinhos_na_escala) {
    // A razao de ser R+B e nao R+G: os vizinhos na escala de gravidade sao
    // amarelo (R+G) e vermelho (R). Uma cor R+G intermediaria viraria um dos
    // dois ao saturar ou apagar o verde.
    EXPECT_EQ(cores::kRosa.g, 0) << "rosa com verde colide com amarelo";
    EXPECT_GT(cores::kRosa.b, 0);
    EXPECT_EQ(cores::kAmarelo.b, 0);
}

TEST(PadraoLed, repetir_a_zona_nao_reinicia_a_fase) {
    // O laco chama isto a cada volta. Reiniciar deixaria o LED preso aceso.
    PadraoLed p;
    for (std::uint32_t t = 0; t <= 400; t += 10) {
        p.define_zona(Zona::Perigo, t);
    }
    EXPECT_EQ(p.cor(400), cores::kApagado) << "ficou preso no aceso";
}

TEST(PadraoLed, trocar_de_zona_recomeca_a_fase) {
    PadraoLed p;
    p.define_zona(Zona::Perigo, 0);
    ASSERT_EQ(p.cor(200), cores::kApagado);   // no vao do ciclo
    p.define_zona(Zona::AproximacaoMargem, 200);
    EXPECT_EQ(p.cor(200), cores::kRosa) << "a zona nova tem de aparecer na hora";
}

TEST(PadraoLed, a_piscada_atravessa_o_estouro_do_relogio) {
    constexpr std::uint32_t kQuase = 0xFFFFFF00U;
    PadraoLed p;
    p.define_zona(Zona::Perigo, kQuase);
    EXPECT_EQ(p.cor(kQuase), cores::kVermelho);
    EXPECT_EQ(p.cor(static_cast<std::uint32_t>(kQuase + 130U)), cores::kApagado);
    EXPECT_EQ(p.cor(static_cast<std::uint32_t>(kQuase + 260U)), cores::kVermelho);
}

}  // namespace
