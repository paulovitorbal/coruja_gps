#include "display/TelaPrincipal.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "nucleo/Geo.h"

namespace {

using namespace coruja;

/// Anota o que foi desenhado, para o teste poder afirmar sobre o layout sem
/// painel. É o mesmo princípio dos outros dublês do projeto: o que se
/// verifica aqui é a decisão de **o que mostrar**, não o SPI.
class VisorEspiao : public Visor {
public:
    struct Ret { int x, y, l, a; Cor565 cor; };
    struct Txt { int x, y; std::string s; Fonte f; Cor565 cor; };
    std::vector<Ret>   retangulos;
    std::vector<Txt>   textos;
    std::vector<Icone> icones;
    unsigned           apresentacoes = 0;

    void retangulo(int x, int y, int l, int a, Cor565 c) override {
        retangulos.push_back({x, y, l, a, c});
    }
    void texto(int x, int y, const char* s, Fonte f, Cor565 c) override {
        textos.push_back({x, y, s, f, c});
    }
    void icone(int, int, Icone i) override { icones.push_back(i); }
    void apresenta() override { ++apresentacoes; }

    void limpa() { retangulos.clear(); textos.clear(); icones.clear();
                   apresentacoes = 0; }

    bool tem_texto(const std::string& parte) const {
        for (const auto& t : textos) {
            if (t.s.find(parte) != std::string::npos) { return true; }
        }
        return false;
    }
    const Txt* numero() const {
        for (const auto& t : textos) {
            if (t.f == Fonte::Numero) { return &t; }
        }
        return nullptr;
    }
    bool tem_cor(Cor565 c) const {
        for (const auto& r : retangulos) { if (r.cor == c) { return true; } }
        return false;
    }
};

Ponto ponto(std::uint8_t limite, TipoPonto tipo = TipoPonto::RadarFixo) {
    Ponto p{};
    p.limite = limite;
    p.tipo = tipo;
    p.sentido = Sentido::Omnidirecional;
    return p;
}

EstadoTela dirigindo(float kmh) {
    EstadoTela e;
    e.tem_fix = true;
    e.telemetria.velocidade_kmh = kmh;
    e.telemetria.data_valida = true;
    e.telemetria.ano = 2026; e.telemetria.mes = 9; e.telemetria.dia = 28;
    e.telemetria.hora = 14; e.telemetria.minuto = 35;
    e.veredito.zona = Zona::Segura;
    return e;
}

// ================================================= geometria e moldura

TEST(TelaPrincipal, as_faixas_do_requisito_fecham_em_240) {
    EXPECT_EQ(tela::kMoldura + tela::kFaixaSuperior + tela::kAreaNumero +
                  tela::kFaixaInferior + tela::kMoldura, 240);
    EXPECT_EQ(tela::kYFaixaInferior + tela::kFaixaInferior + tela::kMoldura,
              tela::kAltura);
}

// ========================================== número e denominador (§4.1)

TEST(TelaPrincipal, zona_segura_mostra_so_a_velocidade_sem_denominador) {
    // Fora do raio de um ponto o aparelho NAO conhece o limite da via: a
    // base e um conjunto de pontos, nao uma malha viaria.
    VisorEspiao v;
    TelaPrincipal tela;
    tela.desenha(dirigindo(75.0F), 0, v);
    ASSERT_NE(v.numero(), nullptr);
    EXPECT_EQ(v.numero()->s, "75");
    EXPECT_EQ(v.numero()->cor, paleta::kTexto);
}

TEST(TelaPrincipal, perto_de_radar_mostra_velocidade_sobre_limite) {
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(75.0F);
    e.veredito.zona = Zona::AproximacaoConforme;
    e.veredito.tem_alvo = true;
    e.veredito.alvo = ponto(110);
    e.veredito.distancia_m = 200.0F;
    tela.desenha(e, 0, v);
    ASSERT_NE(v.numero(), nullptr);
    EXPECT_EQ(v.numero()->s, "75/110");
}

TEST(TelaPrincipal, semaforo_camera_nao_ganha_denominador) {
    // Ele fiscaliza avanco de sinal, nao velocidade: nao ha limite a
    // comparar, e inventar um seria mentir.
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(75.0F);
    e.veredito.zona = Zona::Semaforo;
    e.veredito.tem_alvo = true;
    e.veredito.alvo = ponto(kSemLimite, TipoPonto::SemaforoCamera);
    e.veredito.distancia_m = 150.0F;
    tela.desenha(e, 0, v);
    ASSERT_NE(v.numero(), nullptr);
    EXPECT_EQ(v.numero()->s, "75");
}

TEST(TelaPrincipal, sem_fix_o_numero_vira_tracos_em_cinza) {
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e;
    e.tem_fix = false;
    tela.desenha(e, 0, v);
    ASSERT_NE(v.numero(), nullptr);
    EXPECT_EQ(v.numero()->s, "- -");
    EXPECT_EQ(v.numero()->cor, paleta::kDegradado)
        << "estado degradado nao pode usar a mesma cor do numero valido";
}

// ============================================ faixa inferior: prioridade

TEST(TelaPrincipal, zona_segura_deixa_a_faixa_inferior_VAZIA) {
    // O vazio e a mensagem: e ele que da contraste ao alerta.
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(75.0F);
    // O alvo vem PREENCHIDO de proposito: quem manda e o `tem_alvo`, nao o
    // conteudo. Com um alvo zerado, um `if` errado passaria despercebido
    // porque o tipo invalido nao rende icone nenhum.
    e.veredito.tem_alvo = false;
    e.veredito.alvo = ponto(60);
    e.veredito.distancia_m = 100.0F;
    tela.desenha(e, 0, v);
    EXPECT_TRUE(v.icones.empty());
    EXPECT_FALSE(v.tem_cor(paleta::kBarraAmbar));
    EXPECT_FALSE(v.tem_cor(paleta::kBarraPerigo));
}

TEST(TelaPrincipal, o_aviso_de_OTA_vence_tudo_por_dois_segundos) {
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(75.0F);
    e.base_disponivel = false;        // ocupante de prioridade 2
    e.houve_aviso_ota = true;
    e.aviso_ota_em_ms = 1000;
    tela.desenha(e, 1500, v);
    EXPECT_TRUE(v.tem_texto("PARE O VEICULO"));
    EXPECT_FALSE(v.tem_texto("BASE INDISPONIVEL"));
}

TEST(TelaPrincipal, passados_os_dois_segundos_o_ocupante_seguinte_reassume) {
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(75.0F);
    e.base_disponivel = false;
    e.houve_aviso_ota = true;
    e.aviso_ota_em_ms = 1000;
    tela.desenha(e, 1500, v);
    v.limpa();
    tela.desenha(e, 3100, v);
    EXPECT_TRUE(v.tem_texto("BASE INDISPONIVEL"));
}

TEST(TelaPrincipal, base_ausente_vence_sem_sinal) {
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e;
    e.tem_fix = false;
    e.base_disponivel = false;
    tela.desenha(e, 5000, v);
    EXPECT_TRUE(v.tem_texto("BASE INDISPONIVEL"));
    EXPECT_FALSE(v.tem_texto("SEM SINAL"));
}

TEST(TelaPrincipal, sem_sinal_mostra_o_tempo_decorrido) {
    // 0:14 e um viaduto e 3:20 e problema real -- a acao do motorista
    // difere nos dois casos, e por isso o numero esta la.
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e;
    e.tem_fix = false;
    e.sem_sinal_desde_ms = 1000;
    tela.desenha(e, 15000, v);
    EXPECT_TRUE(v.tem_texto("SEM SINAL"));
    EXPECT_TRUE(v.tem_texto("0:14"));
}

TEST(TelaPrincipal, com_alvo_aparecem_icone_e_barra) {
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(90.0F);
    e.veredito.zona = Zona::Perigo;
    e.veredito.tem_alvo = true;
    e.veredito.alvo = ponto(60);
    e.veredito.distancia_m = 150.0F;
    tela.desenha(e, 0, v);
    ASSERT_EQ(v.icones.size(), 1U);
    EXPECT_EQ(v.icones[0], Icone::RadarFixo);
    EXPECT_TRUE(v.tem_cor(paleta::kBarraPerigo));
}

// ================================================== ícones e cores

TEST(TelaPrincipal, cada_tipo_de_ponto_tem_seu_icone) {
    EXPECT_EQ(icone_de(TipoPonto::RadarFixo), Icone::RadarFixo);
    EXPECT_EQ(icone_de(TipoPonto::RadarMovel), Icone::RadarMovel);
    EXPECT_EQ(icone_de(TipoPonto::SemaforoComRadar), Icone::SemaforoComRadar);
    EXPECT_EQ(icone_de(TipoPonto::SemaforoCamera), Icone::Semaforo);
}

TEST(TelaPrincipal, radar_movel_se_distingue_do_fixo) {
    // RF03.5: a zona e identica a de um radar fixo, mas o motorista precisa
    // saber que aquele ponto pode nao estar la hoje.
    EXPECT_NE(icone_de(TipoPonto::RadarMovel), icone_de(TipoPonto::RadarFixo));
}

TEST(TelaPrincipal, as_tres_cores_de_barra_sao_distintas) {
    const Cor565 a = cor_da_barra(Zona::AproximacaoConforme);
    const Cor565 m = cor_da_barra(Zona::AproximacaoMargem);
    const Cor565 p = cor_da_barra(Zona::Perigo);
    EXPECT_NE(a, m);
    EXPECT_NE(m, p);
    EXPECT_NE(a, p);
}

TEST(TelaPrincipal, semaforo_usa_a_mesma_ambar_da_aproximacao_conforme) {
    // Sao tres cores e nao quatro: verde nao aparece na tela, porque Zona
    // Segura nao tem barra.
    EXPECT_EQ(cor_da_barra(Zona::Semaforo),
              cor_da_barra(Zona::AproximacaoConforme));
}

TEST(TelaPrincipal, a_barra_enche_conforme_se_aproxima) {
    EXPECT_EQ(preenchimento(300.0F), 0);
    EXPECT_EQ(preenchimento(0.0F), 100);
    EXPECT_EQ(preenchimento(400.0F), 0) << "fora do raio nao passa de vazia";
    // Ponto ASSIMETRICO: a 150 m a conta invertida daria os mesmos 50, e os
    // extremos sao mascarados pelos limites. So 75 m distingue.
    EXPECT_EQ(preenchimento(75.0F), 75) << "a barra esta enchendo ao contrario";
    EXPECT_EQ(preenchimento(225.0F), 25);
}

// ============================================ faixa superior e relógio

TEST(TelaPrincipal, o_relogio_vem_do_GPS_em_UTC_menos_3) {
    Telemetria t;
    t.data_valida = true;
    t.ano = 2026; t.mes = 9; t.dia = 28; t.hora = 14; t.minuto = 35;
    char buf[32];
    formata_relogio(t, buf, sizeof buf);
    EXPECT_STREQ(buf, "28/09/26 11:35");
}

TEST(TelaPrincipal, o_fuso_pode_voltar_o_dia_e_o_mes) {
    // 01/10 as 01:00 UTC e 30/09 as 22:00 aqui. Sem isto o relogio erraria
    // o dia por tres horas toda noite.
    Telemetria t;
    t.data_valida = true;
    t.ano = 2026; t.mes = 10; t.dia = 1; t.hora = 1; t.minuto = 5;
    char buf[32];
    formata_relogio(t, buf, sizeof buf);
    EXPECT_STREQ(buf, "30/09/26 22:05");
}

TEST(TelaPrincipal, sem_fix_o_relogio_fica_em_tracos) {
    // Nao ha RTC com bateria no BOM: o relogio vem do GPS ou nao vem.
    Telemetria t;
    char buf[32];
    formata_relogio(t, buf, sizeof buf);
    EXPECT_STREQ(buf, "--/--/-- --:--");
}

TEST(TelaPrincipal, taxa_reduzida_vence_o_relogio_na_faixa_superior) {
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(75.0F);
    e.taxa = EstadoTaxa::Degradado;
    tela.desenha(e, 0, v);
    EXPECT_TRUE(v.tem_texto("TAXA DE GPS REDUZIDA"));
    EXPECT_FALSE(v.tem_texto("28/09/26"));
}

TEST(TelaPrincipal, a_barra_de_brilho_e_transitoria) {
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(75.0F);
    e.houve_ajuste_brilho = true;
    e.brilho_mexido_em_ms = 1000;
    e.brilho_pct = 45;
    tela.desenha(e, 1200, v);
    EXPECT_TRUE(v.tem_texto("BRILHO 45%"));
    v.limpa();
    tela.desenha(e, 3000, v);
    EXPECT_TRUE(v.tem_texto("28/09/26")) << "o relogio tinha de reassumir";
}

// ================================================= redesenho parcial

TEST(TelaPrincipal, o_primeiro_desenho_pinta_tudo) {
    VisorEspiao v;
    TelaPrincipal tela;
    EXPECT_GE(tela.desenha(dirigindo(75.0F), 0, v), 4);
}

TEST(TelaPrincipal, sem_mudanca_nao_redesenha_nada) {
    // A 4 Hz, redesenhar a tela inteira custaria 38,4 ms de SPI por volta
    // contra 3,4 ms da barra. E nada pisca, entao a maior parte das voltas
    // nao tem o que mostrar de novo.
    VisorEspiao v;
    TelaPrincipal tela;
    const EstadoTela e = dirigindo(75.0F);
    tela.desenha(e, 0, v);
    v.limpa();
    EXPECT_EQ(tela.desenha(e, 250, v), 0);
    EXPECT_TRUE(v.retangulos.empty());
    EXPECT_EQ(v.apresentacoes, 0U);
}

TEST(TelaPrincipal, mudar_so_a_velocidade_redesenha_so_o_numero) {
    VisorEspiao v;
    TelaPrincipal tela;
    tela.desenha(dirigindo(75.0F), 0, v);
    v.limpa();
    EXPECT_EQ(tela.desenha(dirigindo(76.0F), 250, v), 1);
    ASSERT_NE(v.numero(), nullptr);
    EXPECT_EQ(v.numero()->s, "76");
}

TEST(TelaPrincipal, invalida_forca_o_redesenho_completo) {
    VisorEspiao v;
    TelaPrincipal tela;
    const EstadoTela e = dirigindo(75.0F);
    tela.desenha(e, 0, v);
    tela.invalida();
    v.limpa();
    EXPECT_GE(tela.desenha(e, 250, v), 4);
}

TEST(TelaPrincipal, o_relogio_mudando_nao_redesenha_o_numero) {
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(75.0F);
    tela.desenha(e, 0, v);
    v.limpa();
    e.telemetria.minuto = 36;
    EXPECT_EQ(tela.desenha(e, 250, v), 1);
    EXPECT_EQ(v.numero(), nullptr) << "repintou o numero a toa";
}

}  // namespace
