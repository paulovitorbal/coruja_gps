#include "display/TelaPrincipal.h"

#include <cstdlib>

#include "display/Sprites.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "display/TextoRolante.h"
#include "nucleo/Geo.h"

namespace {

using namespace coruja;

/// Anota o que foi desenhado, para o teste poder afirmar sobre o layout sem
/// painel. É o mesmo princípio dos outros dublês do projeto: o que se
/// verifica aqui é a decisão de **o que mostrar**, não o SPI.
class VisorEspiao : public Visor {
public:
    struct Ret { int x, y, l, a; Cor565 cor; };
    struct Txt { int x, y; std::string s; Fonte f; Cor565 cor;
                 Alinhamento alin; };
    std::vector<Ret>   retangulos;
    std::vector<Txt>   textos;
    std::vector<Icone> icones;
    unsigned           apresentacoes = 0;

    void retangulo(int x, int y, int l, int a, Cor565 c) override {
        retangulos.push_back({x, y, l, a, c});
    }
    void texto(int x, int y, const char* s, Fonte f, Cor565 c,
               Alinhamento a) override {
        textos.push_back({x, y, s, f, c, a});
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
    /// O `/limite`, que desde o R-64 vai em fonte propria e menor.
    const Txt* limite() const {
        for (const auto& t : textos) {
            if (t.f == Fonte::NumeroPequeno) { return &t; }
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
    EXPECT_EQ(v.numero()->s, "75");
    // Desde o R-64 o denominador e desenhado a parte, em fonte menor:
    // "120/120" numa fonte so daria 392 px numa tela de 320.
    ASSERT_NE(v.limite(), nullptr) << "o denominador sumiu";
    EXPECT_EQ(v.limite()->s, "/110");
    // Assentam na mesma linha de base, e o limite vem DEPOIS da velocidade.
    EXPECT_GT(v.limite()->x, v.numero()->x);
    EXPECT_GT(v.limite()->y, v.numero()->y)
        << "o limite nao assentou na linha de base do numero";
}

TEST(TelaPrincipal, velocidade_e_limite_sao_centralizados_como_conjunto) {
    // Medir so o numero deslocaria o par para a esquerda quando houvesse
    // limite, e o conjunto dancaria ao entrar e sair de alerta -- numa tela
    // que se olha de relance, movimento sem significado custa atencao.
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(75.0F);
    e.veredito.zona = Zona::AproximacaoConforme;
    e.veredito.tem_alvo = true;
    e.veredito.alvo = ponto(110);
    e.veredito.distancia_m = 200.0F;
    tela.desenha(e, 0, v);

    const int largura_total =
        largura_da_fonte(Fonte::Numero, "75") +
        largura_da_fonte(Fonte::NumeroPequeno, "/110");
    const int esperado = (tela::kLargura - largura_total) / 2;
    EXPECT_EQ(v.numero()->x, esperado);
    EXPECT_EQ(v.numero()->alin, Alinhamento::Esquerda)
        << "centralizacao do conjunto e feita aqui, nao no Visor";
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
    EXPECT_TRUE(v.tem_texto("PARE PARA ATUALIZAR"));
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
    EXPECT_EQ(v.icones[0], Icone::Radar);
    EXPECT_TRUE(v.tem_cor(paleta::kBarraPerigo));
}

// ================================================== ícones e cores

TEST(TelaPrincipal, cada_tipo_de_ponto_tem_seu_icone) {
    EXPECT_EQ(icone_de(TipoPonto::RadarFixo), Icone::Radar);
    EXPECT_EQ(icone_de(TipoPonto::SemaforoComRadar), Icone::SemaforoComRadar);
    EXPECT_EQ(icone_de(TipoPonto::SemaforoCamera), Icone::Semaforo);
}

TEST(TelaPrincipal, radar_movel_mostra_o_MESMO_icone_do_fixo) {
    // Decidido em 2026-09-28. O RF03.5 ja zoneia o movel igual ao fixo, e a
    // acao do motorista e a mesma nos dois: distincao que nao muda decisao e
    // ruido no instante em que menos se pode gastar atencao.
    EXPECT_EQ(icone_de(TipoPonto::RadarMovel), icone_de(TipoPonto::RadarFixo));
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

TEST(TelaPrincipal, o_ocupante_da_faixa_superior_fica_centralizado) {
    // A faixa tem um ocupante por vez e nada em volta: centralizada, ela
    // equilibra com o numero, que tambem e centralizado.
    VisorEspiao v;
    TelaPrincipal tela;
    tela.desenha(dirigindo(75.0F), 0, v);
    bool achou = false;
    for (const auto& t : v.textos) {
        if (t.f != Fonte::TextoGrande) { continue; }
        if (t.s.find("28/09/26") == std::string::npos) { continue; }
        // A centragem passou da `Visor` para a tela quando o texto ganhou
        // rolagem: quem decide o `x` e quem sabe se o texto cabe.
        EXPECT_EQ(t.alin, Alinhamento::Esquerda);
        EXPECT_EQ(t.x, (tela::kLargura -
                        largura_da_fonte(Fonte::TextoGrande, t.s.c_str())) / 2);
        achou = true;
    }
    EXPECT_TRUE(achou);
}

TEST(TelaPrincipal, a_faixa_superior_usa_a_fonte_maior) {
    // Relatado dirigindo, em 2026-10-06: data e hora dificeis de ler.
    VisorEspiao v;
    TelaPrincipal tela;
    tela.desenha(dirigindo(75.0F), 0, v);

    bool achou = false;
    for (const auto& t : v.textos) {
        if (t.s.find("28/09/26") == std::string::npos) { continue; }
        EXPECT_EQ(t.f, Fonte::TextoGrande);
        achou = true;
    }
    EXPECT_TRUE(achou) << "o relogio nao foi desenhado";
    EXPECT_GT(altura_da_fonte(Fonte::TextoGrande), altura_da_fonte(Fonte::Texto));
}

TEST(TelaPrincipal, a_fonte_maior_cabe_na_faixa_e_nas_frases) {
    // ⚠️ As duas restricoes que ditaram 14x23, e que um aumento futuro
    // quebraria em silencio:
    //
    //   altura — a faixa tem 26 px, e fonte mais alta vazaria sobre o numero;
    //   largura — "TAXA DE GPS REDUZIDA" e a frase mais longa da faixa, e
    //             passando de 320 px ela comecaria a ROLAR para dizer o que
    //             hoje se le de uma vez.
    EXPECT_LE(altura_da_fonte(Fonte::TextoGrande), tela::kFaixaSuperior);
    EXPECT_LE(largura_da_fonte(Fonte::TextoGrande, "TAXA DE GPS REDUZIDA"),
              tela::kLargura);
    EXPECT_LE(largura_da_fonte(Fonte::TextoGrande, "BRILHO 100%"),
              tela::kLargura);
}

TEST(TelaPrincipal, a_faixa_inferior_fica_a_esquerda) {
    // O contador de tempo sem sinal cresce ao fim da linha; centralizado,
    // ele arrastaria a frase inteira de lado a cada segundo.
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e;
    e.tem_fix = false;
    tela.desenha(e, 5000, v);
    for (const auto& t : v.textos) {
        if (t.s.find("SEM SINAL") != std::string::npos) {
            EXPECT_EQ(t.alin, Alinhamento::Esquerda);
        }
    }
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

// ---------------------------------------- a barra so existe em alerta

TEST(TelaPrincipal, sem_alvo_nao_ha_barra_nem_icone) {
    // **A faixa inferior vazia E a mensagem.** Barra so existe perto de
    // ponto -- dentro dos 300 m do kRaioAlertaM -- e e o vazio que da
    // contraste ao alerta quando ele aparece. Uma barra permanente viraria
    // moldura, e moldura o olho para de ver.
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(75.0F);
    e.veredito.zona = Zona::Segura;
    e.veredito.tem_alvo = false;
    tela.desenha(e, 0, v);

    EXPECT_TRUE(v.icones.empty()) << "icone sem alvo";
    for (const auto& r : v.retangulos) {
        EXPECT_NE(r.cor, paleta::kBarraAmbar);
        EXPECT_NE(r.cor, paleta::kBarraRosa);
        EXPECT_NE(r.cor, paleta::kBarraPerigo);
        EXPECT_NE(r.cor, paleta::kMoldura) << "trilho da barra sem alvo";
    }
}

TEST(TelaPrincipal, com_alvo_a_barra_e_o_icone_aparecem) {
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(75.0F);
    e.veredito.zona = Zona::AproximacaoConforme;
    e.veredito.tem_alvo = true;
    e.veredito.alvo = ponto(60);
    e.veredito.distancia_m = 150.0F;
    tela.desenha(e, 0, v);

    ASSERT_EQ(v.icones.size(), 1U);
    EXPECT_EQ(v.icones[0], Icone::Radar);
    EXPECT_TRUE(v.tem_cor(paleta::kBarraAmbar));
}

TEST(TelaPrincipal, perder_o_alvo_apaga_a_barra) {
    // O caminho de volta importa tanto quanto o de ida: uma barra que fica
    // na tela depois de o ponto passar diria que ainda ha algo a frente.
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(75.0F);
    e.veredito.zona = Zona::AproximacaoConforme;
    e.veredito.tem_alvo = true;
    e.veredito.alvo = ponto(60);
    e.veredito.distancia_m = 150.0F;
    tela.desenha(e, 0, v);
    ASSERT_FALSE(v.icones.empty());

    v.limpa();
    e.veredito.zona = Zona::Segura;
    e.veredito.tem_alvo = false;
    tela.desenha(e, 0, v);

    EXPECT_TRUE(v.icones.empty()) << "o icone sobreviveu ao alvo";
    EXPECT_FALSE(v.tem_cor(paleta::kBarraAmbar)) << "a barra sobreviveu";
}

TEST(TelaPrincipal, nao_ha_mais_moldura) {
    // Os dois fios de 2 px nao carregavam informacao e gastavam area
    // acesa. Removidos em 2026-09-29, a pedido do autor.
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(75.0F);
    e.veredito.tem_alvo = false;
    tela.desenha(e, 0, v);
    for (const auto& r : v.retangulos) {
        const bool e_faixa_fina = r.a == tela::kMoldura &&
                                  r.l == tela::kLargura;
        EXPECT_FALSE(e_faixa_fina) << "moldura voltou em y=" << r.y;
    }
}

// ======================================= fundo claro da faixa com alerta
//
// Relatado dirigindo, em 2026-10-06: o icone ficava dificil de ler no preto
// sob luz do dia.

namespace {
/// O retangulo que ocupa a faixa inferior inteira, se houver.
const VisorEspiao::Ret* faixa_inferior(const VisorEspiao& v) {
    for (const auto& r : v.retangulos) {
        if (r.y == tela::kYFaixaInferior && r.x == 0 &&
            r.l == tela::kLargura && r.a == tela::kFaixaInferior) {
            return &r;
        }
    }
    return nullptr;
}
}  // namespace

TEST(TelaPrincipal, com_alerta_a_faixa_inferior_fica_clara) {
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(70.0F);
    e.veredito.zona = Zona::AproximacaoConforme;
    e.veredito.tem_alvo = true;
    e.veredito.alvo = ponto(60);
    e.veredito.distancia_m = 150.0F;
    tela.desenha(e, 1000, v);

    const auto* faixa = faixa_inferior(v);
    ASSERT_NE(faixa, nullptr) << "a faixa inferior nao foi pintada";
    EXPECT_EQ(faixa->cor, paleta::kFundoAlerta);
}

TEST(TelaPrincipal, sem_alerta_a_faixa_inferior_segue_preta) {
    VisorEspiao v;
    TelaPrincipal tela;
    tela.desenha(dirigindo(70.0F), 1000, v);

    const auto* faixa = faixa_inferior(v);
    ASSERT_NE(faixa, nullptr);
    EXPECT_EQ(faixa->cor, paleta::kFundo);
}

TEST(TelaPrincipal, texto_na_faixa_nao_ganha_fundo_claro) {
    // Branco sobre creme teria contraste PIOR que o de hoje. Os estados de
    // texto ficam no preto — a troca e so para o icone.
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(0.0F);
    e.tem_fix = false;
    e.sem_sinal_desde_ms = 0;
    tela.desenha(e, 14000, v);

    const auto* faixa = faixa_inferior(v);
    ASSERT_NE(faixa, nullptr);
    EXPECT_EQ(faixa->cor, paleta::kFundo) << "SEM SINAL e texto branco";
}

TEST(TelaPrincipal, o_sprite_foi_composto_sobre_a_cor_da_faixa) {
    // ⚠️ A guarda que importa. O sprite nao tem canal alfa: o transparente do
    // PNG e achatado contra uma cor no `gera_sprites.py`, e se ela divergir do
    // `kFundoAlerta` o icone ganha uma moldura de 40x40.
    //
    // As quinas do sprite sao transparentes no PNG original, entao elas SAO o
    // fundo — e e por isso que da para conferir a composicao sem renderizar.
    // Tolerancia de 1 LSB por canal, e nao igualdade exata: o
    // redimensionamento usa LANCZOS, que sangra um pouco nas bordas, e uma das
    // quinas sai com 0xF75B contra 0xF75C. Em RGB565 isso e um degrau
    // imperceptivel.
    //
    // A folga nao enfraquece o teste: a falha que ele existe para pegar e
    // compor sobre PRETO quando a faixa e creme, que erra por 30 degraus de
    // vermelho, nao por um.
    auto perto_do_fundo = [](std::uint16_t c) {
        const int dr = ((c >> 11) & 0x1F) - ((paleta::kFundoAlerta >> 11) & 0x1F);
        const int dg = ((c >> 5) & 0x3F) - ((paleta::kFundoAlerta >> 5) & 0x3F);
        const int db = (c & 0x1F) - (paleta::kFundoAlerta & 0x1F);
        return std::abs(dr) <= 1 && std::abs(dg) <= 1 && std::abs(db) <= 1;
    };

    const int ultimo = sprite::kLado * sprite::kLado - 1;
    for (const auto* arte : {sprite::kRadar, sprite::kSemaforo,
                             sprite::kSemaforoComRadar}) {
        EXPECT_TRUE(perto_do_fundo(arte[0]))
            << "quina superior esquerda: 0x" << std::hex << arte[0];
        EXPECT_TRUE(perto_do_fundo(arte[ultimo]))
            << "quina inferior direita: 0x" << std::hex << arte[ultimo];
    }
}

// ------------------------------------------ texto que nao cabe rola

TEST(TelaPrincipal, a_frase_de_sem_sinal_e_so_o_essencial) {
    // "alertas suspensos" era inferencia do proprio "sem sinal", e levava a
    // frase a 432 px numa tela de 320 -- custava rolagem para dizer o que o
    // leitor ja sabia.
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(0.0F);
    e.tem_fix = false;
    e.sem_sinal_desde_ms = 0;
    tela.desenha(e, 14000, v);

    EXPECT_TRUE(v.tem_texto("SEM SINAL"));
    EXPECT_TRUE(v.tem_texto("0:14")) << "o contador sumiu";
    EXPECT_FALSE(v.tem_texto("alertas suspensos"));

    // E, encurtada, ela CABE -- o que e o ponto da mudanca.
    for (const auto& t : v.textos) {
        if (t.s.find("SEM SINAL") == std::string::npos) { continue; }
        EXPECT_LE(largura_da_fonte(Fonte::Texto, t.s.c_str()),
                  tela::kLargura)
            << "a frase encurtada ainda nao cabe: " << t.s;
    }
}

TEST(TelaPrincipal, nenhuma_frase_da_tela_principal_estoura) {
    // **O invariante que importa.** A fonte de texto tem 12 px por glifo e
    // a tela 320: o teto e 26 caracteres. Seis das nove frases do projeto
    // estouravam esse teto, e o painel descartava glifos INTEIROS em
    // silencio nas duas pontas -- nada avisava que a frase estava
    // incompleta.
    //
    // A rolagem existe como rede de seguranca, nao como projeto: texto que
    // rola e mais lento de ler e custa SPI. Este teste afirma que a tela
    // principal nao depende dela.
    VisorEspiao v;
    TelaPrincipal tela;

    struct Caso { const char* nome; EstadoTela e; };
    EstadoTela sem_fix = dirigindo(0.0F);
    sem_fix.tem_fix = false;
    EstadoTela sem_base = dirigindo(55.0F);
    sem_base.base_disponivel = false;
    EstadoTela aviso = dirigindo(60.0F);
    aviso.houve_aviso_ota = true;
    aviso.aviso_ota_em_ms = 0;
    EstadoTela taxa = dirigindo(60.0F);
    taxa.taxa = EstadoTaxa::Degradado;
    EstadoTela alerta = dirigindo(120.0F);
    alerta.veredito.zona = Zona::Perigo;
    alerta.veredito.tem_alvo = true;
    alerta.veredito.alvo = ponto(120);
    alerta.veredito.distancia_m = 90.0F;

    const Caso casos[] = {{"sem fix", sem_fix}, {"sem base", sem_base},
                          {"aviso de OTA", aviso}, {"taxa reduzida", taxa},
                          {"120/120", alerta}};
    for (const auto& c : casos) {
        v.limpa();
        tela.invalida();
        tela.desenha(c.e, 14000, v);
        for (const auto& t : v.textos) {
            if (t.f != Fonte::Texto) { continue; }
            EXPECT_LE(largura_da_fonte(Fonte::Texto, t.s.c_str()),
                      tela::kLargura)
                << "estourou em '" << c.nome << "': " << t.s;
        }
    }
}

TEST(TelaPrincipal, o_texto_que_cabe_nao_anda) {
    // Movimento sem informacao custa atencao numa tela que se olha de
    // relance. "TAXA DE GPS REDUZIDA" tem 240 px e cabe.
    VisorEspiao v;
    TelaPrincipal tela;
    EstadoTela e = dirigindo(60.0F);
    e.taxa = EstadoTaxa::Degradado;

    tela.desenha(e, 0, v);
    int x1 = 999;
    for (const auto& t : v.textos) {
        if (t.s.find("TAXA") != std::string::npos) { x1 = t.x; }
    }
    ASSERT_NE(x1, 999);

    v.limpa();
    const int regioes = tela.desenha(e, 30000, v);
    EXPECT_EQ(regioes, 0) << "redesenhou um texto que nao rola";
}

}  // namespace
