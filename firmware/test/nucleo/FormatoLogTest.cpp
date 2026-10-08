#include "nucleo/FormatoLog.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace {

using namespace coruja;

RegistroInfracao infracao_exemplo() {
    RegistroInfracao r;
    r.momento.ano = 2026; r.momento.mes = 10; r.momento.dia = 4;
    r.momento.hora = 17; r.momento.minuto = 31; r.momento.segundo = 12;
    r.momento.lat = -19.799916F;
    r.momento.lon = -44.021044F;
    r.momento.rumo_graus = 257.0F;
    r.momento.rumo_valido = true;
    r.momento.velocidade_kmh = 71.2F;
    r.v_max_kmh = 94.8F;
    r.v_infra_kmh = 66.0F;
    r.radar = Ponto{-19.799920F, -44.021050F, 60, 0, TipoPonto::RadarFixo,
                    Sentido::Omnidirecional};
    r.dist_min_m = 12.4F;
    return r;
}

PontoViagem ponto_exemplo() {
    PontoViagem p;
    p.ano = 2026; p.mes = 10; p.dia = 4; p.hora = 17; p.minuto = 30;
    p.lat = -19.8043F;
    p.lon = -44.0298F;
    p.v_media_kmh = 58.1F;
    p.dist_km = 12.40F;
    return p;
}

// --- linhas de referencia, calculadas FORA do codigo ---

TEST(FormatoLog, a_linha_de_infracao_bate_com_a_referencia) {
    char linha[kTamLinhaInfracao];
    const auto n = formata_infracao(infracao_exemplo(), linha, sizeof linha);

    ASSERT_GT(n, 0u);
    EXPECT_STREQ(linha,
                 "2026-10-04T17:31:12Z;-19.79992;-44.02105;257;"
                 "71.2;94.8;66.0;60;-19.79992;-44.02105;12.4\n");
    EXPECT_EQ(n, std::strlen(linha));
}

TEST(FormatoLog, a_linha_de_viagem_bate_com_a_referencia) {
    char linha[kTamLinhaViagem];
    const auto n = formata_ponto_viagem(ponto_exemplo(), linha, sizeof linha);

    ASSERT_GT(n, 0u);
    // v3: quatro colunas a mais, VAZIAS porque este exemplo nao tem radar.
    EXPECT_STREQ(linha,
                 "2026-10-04T17:30:00Z;-19.80430;-44.02980;58.1;12.40;;;;\n");
    EXPECT_EQ(n, std::strlen(linha));
}

/// Quantos campos a linha tem, separados por `;`.
std::size_t campos(const char* linha) {
    std::size_t n = 1;
    for (const char* p = linha; *p != '\0'; ++p) {
        if (*p == ';') { ++n; }
    }
    return n;
}

TEST(FormatoLog, a_linha_tem_o_MESMO_numero_de_campos_com_e_sem_radar) {
    // A propriedade da qual todo leitor de CSV depende. Um campo a mais ou a
    // menos conforme houvesse radar deslocaria as colunas e faria o
    // analisador ler distancia como limite -- em silencio, porque os dois
    // sao numeros.
    char sem[kTamLinhaViagem];
    formata_ponto_viagem(ponto_exemplo(), sem, sizeof sem);

    PontoViagem com = ponto_exemplo();
    com.radar.tem_alerta = true;
    com.radar.dist_alerta_m = 123.4F;
    com.radar.limite_alerta = 60;
    com.radar.tem_proximo = true;
    com.radar.dist_proximo_m = 45.6F;
    com.radar.limite_proximo = 80;
    char cheio[kTamLinhaViagem];
    formata_ponto_viagem(com, cheio, sizeof cheio);

    EXPECT_EQ(campos(sem), campos(cheio));
    EXPECT_EQ(campos(cheio), 9u) << "o cabecalho v3 declara nove colunas";
}

TEST(FormatoLog, o_radar_do_eixao_sai_com_os_dois_pontos_distintos) {
    // O caso que motivou as colunas: a 80 km/h na pista principal, o alerta
    // vem do radar de 60 da pista LATERAL (venceu por gravidade) enquanto o
    // mais proximo e outro. Sem as duas colunas nao da para ver isso.
    PontoViagem p = ponto_exemplo();
    p.v_media_kmh = 80.0F;
    p.radar.tem_alerta = true;
    p.radar.dist_alerta_m = 210.5F;
    p.radar.limite_alerta = 60;
    p.radar.tem_proximo = true;
    p.radar.dist_proximo_m = 88.2F;
    p.radar.limite_proximo = 80;

    char linha[kTamLinhaViagem];
    ASSERT_GT(formata_ponto_viagem(p, linha, sizeof linha), 0u);
    EXPECT_STREQ(linha,
                 "2026-10-04T17:30:00Z;-19.80430;-44.02980;80.0;12.40;"
                 "210.5;60;88.2;80\n");
}

TEST(FormatoLog, limite_zero_de_semaforo_nao_se_confunde_com_vazio) {
    // Zero e limite VALIDO: e o do semaforo. Se "nenhum radar" tambem saisse
    // como zero, as duas situacoes virariam a mesma linha.
    PontoViagem p = ponto_exemplo();
    p.radar.tem_alerta = true;
    p.radar.dist_alerta_m = 50.0F;
    p.radar.limite_alerta = 0;

    char linha[kTamLinhaViagem];
    ASSERT_GT(formata_ponto_viagem(p, linha, sizeof linha), 0u);
    EXPECT_NE(std::string(linha).find(";50.0;0;;"), std::string::npos)
        << linha;
}

TEST(FormatoLog, so_o_mais_proximo_sem_alerta_ainda_preenche_as_colunas_certas) {
    // Os dois lados sao independentes: pode haver ponto mais proximo sem
    // alvo escolhido (todos filtrados por sentido, por exemplo).
    PontoViagem p = ponto_exemplo();
    p.radar.tem_proximo = true;
    p.radar.dist_proximo_m = 77.7F;
    p.radar.limite_proximo = 40;

    char linha[kTamLinhaViagem];
    ASSERT_GT(formata_ponto_viagem(p, linha, sizeof linha), 0u);
    EXPECT_NE(std::string(linha).find(";12.40;;;77.7;40"), std::string::npos)
        << linha;
}

TEST(FormatoLog, o_cabecalho_de_viagem_declara_v3_e_as_nove_colunas) {
    const std::string c = kCabecalhoViagem;
    EXPECT_NE(c.find("viagem v3"), std::string::npos);
    EXPECT_NE(c.find("radar_m;radar_kmh;perto_m;perto_kmh"), std::string::npos);
}

TEST(FormatoLog, o_ponto_de_viagem_tem_sempre_segundo_zero) {
    // A linha descreve o MINUTO, nao um instante dentro dele. O segundo e
    // literal no formato, e nao vem do `PontoViagem` -- que nem o tem.
    char linha[kTamLinhaViagem];
    formata_ponto_viagem(ponto_exemplo(), linha, sizeof linha);
    EXPECT_NE(std::string(linha).find(":00Z;"), std::string::npos);
}

// --- o rumo invalido ---

TEST(FormatoLog, rumo_invalido_vira_999_e_nao_zero) {
    // Parado o NEO-M8N deixa o campo de rumo vazio. Gravar zero diria
    // "apontando para o norte", que e afirmacao falsa e indistinguivel de um
    // rumo norte verdadeiro.
    auto r = infracao_exemplo();
    r.momento.rumo_valido = false;
    r.momento.rumo_graus = 0.0F;

    char linha[kTamLinhaInfracao];
    formata_infracao(r, linha, sizeof linha);
    EXPECT_NE(std::string(linha).find(";999;"), std::string::npos);
}

TEST(FormatoLog, o_rumo_e_arredondado_e_nao_truncado) {
    auto r = infracao_exemplo();
    r.momento.rumo_graus = 256.7F;
    char linha[kTamLinhaInfracao];
    formata_infracao(r, linha, sizeof linha);
    EXPECT_NE(std::string(linha).find(";257;"), std::string::npos);
}

TEST(FormatoLog, rumo_quase_360_vira_ZERO_e_nao_360) {
    // Achado pela previa no host em 2026-10-05: o Eixao corre quase
    // norte-sul, e uma passagem saiu com `rumo 360` no arquivo. Nao existe:
    // a faixa e 0 a 359. O arredondamento de 359,7 da 360,2, e o corte para
    // inteiro deixa 360 -- um digito do sentinela 999 de rumo invalido.
    auto r = infracao_exemplo();
    for (float g : {359.5F, 359.7F, 359.99F}) {
        r.momento.rumo_graus = g;
        char linha[kTamLinhaInfracao];
        formata_infracao(r, linha, sizeof linha);
        EXPECT_NE(std::string(linha).find(";0;"), std::string::npos)
            << g << " graus viraram: " << linha;
        EXPECT_EQ(std::string(linha).find(";360;"), std::string::npos)
            << "escreveu 360, que nao e rumo";
    }
}

TEST(FormatoLog, os_rumos_das_bordas_saem_certos) {
    auto r = infracao_exemplo();
    const struct { float entra; const char* sai; } casos[] = {
        {0.0F, ";0;"}, {0.4F, ";0;"}, {0.5F, ";1;"},
        {180.0F, ";180;"}, {359.0F, ";359;"}, {359.4F, ";359;"},
    };
    for (const auto& c : casos) {
        r.momento.rumo_graus = c.entra;
        char linha[kTamLinhaInfracao];
        formata_infracao(r, linha, sizeof linha);
        EXPECT_NE(std::string(linha).find(c.sai), std::string::npos)
            << c.entra << " deveria sair como " << c.sai << ": " << linha;
    }
}

// --- o que acontece quando nao cabe ---

TEST(FormatoLog, nao_coube_devolve_zero_em_vez_de_truncar) {
    // Linha truncada num CSV e pior que linha ausente: o analisador a aceita
    // e produz numero errado em silencio.
    char curto[20];
    EXPECT_EQ(formata_infracao(infracao_exemplo(), curto, sizeof curto), 0u);
    EXPECT_EQ(formata_ponto_viagem(ponto_exemplo(), curto, sizeof curto), 0u);
}

TEST(FormatoLog, destino_nulo_ou_vazio_devolve_zero) {
    EXPECT_EQ(formata_infracao(infracao_exemplo(), nullptr, 100), 0u);
    char c[1];
    EXPECT_EQ(formata_ponto_viagem(ponto_exemplo(), c, 0), 0u);
}

TEST(FormatoLog, os_buffers_declarados_dao_conta_do_pior_caso) {
    // Longitude de tres digitos com sinal, velocidades de tres digitos,
    // distancia de cinco. Se o pior caso nao couber, a linha some do arquivo
    // justamente no evento mais extremo.
    RegistroInfracao r = infracao_exemplo();
    r.momento.lat = -179.999999F;
    r.momento.lon = -179.999999F;
    r.momento.velocidade_kmh = 299.9F;
    r.v_max_kmh = 299.9F;
    r.v_infra_kmh = 299.9F;
    r.radar.limite = 255;
    r.radar.lat = -179.999999F;
    r.radar.lon = -179.999999F;
    r.dist_min_m = 999.9F;
    char linha[kTamLinhaInfracao];
    EXPECT_GT(formata_infracao(r, linha, sizeof linha), 0u);

    PontoViagem p = ponto_exemplo();
    p.lat = -179.999999F;
    p.lon = -179.999999F;
    p.v_media_kmh = 299.9F;
    p.dist_km = 99999.99F;
    char lv[kTamLinhaViagem];
    EXPECT_GT(formata_ponto_viagem(p, lv, sizeof lv), 0u);
}

}  // namespace
