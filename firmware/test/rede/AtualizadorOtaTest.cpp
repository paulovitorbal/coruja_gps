#include "rede/AtualizadorOta.h"

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <fstream>
#include <string>

#include <cstring>
#include <string>
#include <vector>

#include "apoio/CaoDeGuardaFalso.h"
#include "apoio/LoggerMock.h"
#include "nucleo/BaseRadares.h"
#include "nucleo/Crc32.h"

namespace {

using namespace coruja;

// ============================================================ base sintética

/// Monta um `radares.bin` válido. O caminho de sucesso do OTA só é alcançável
/// com uma base que passe no verificador, então o teste precisa saber montar
/// uma — e montar uma **inválida** de propósito é como se chega à recusa.
std::vector<std::uint8_t> base_valida(std::uint32_t pontos = 3) {
    std::vector<std::uint8_t> reg;
    auto u32 = [](std::vector<std::uint8_t>& v, std::uint32_t x) {
        v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
        v.push_back((x >> 16) & 0xFF); v.push_back((x >> 24) & 0xFF);
    };
    for (std::uint32_t i = 0; i < pontos; ++i) {
        // latitude crescente: a base tem de sair ordenada
        u32(reg, static_cast<std::uint32_t>(-1585000 + static_cast<int>(i) * 100));
        u32(reg, static_cast<std::uint32_t>(-4793000));
        reg.push_back(60);                       // limite
        reg.push_back(0);                        // rumo_q
        reg.push_back(static_cast<std::uint8_t>((0U) | (1U << 2)));  // omni, radar fixo
        reg.push_back(0);
    }
    std::vector<std::uint8_t> out;
    u32(out, kMagic);
    out.push_back(kVersao & 0xFF); out.push_back((kVersao >> 8) & 0xFF);
    out.push_back(kExpoenteEscala);
    out.push_back(kTamRegistro);
    u32(out, pontos);
    u32(out, crc32(reg.data(), reg.size()));
    // Data da base (v2). O firmware aceita v1 tambem, e ha teste proprio
    // para isso em BaseRadaresTest.
    out.push_back(2026 & 0xFF); out.push_back((2026 >> 8) & 0xFF);
    out.push_back(9);
    out.push_back(30);
    out.insert(out.end(), reg.begin(), reg.end());
    return out;
}

// ================================================================== dublês

class ArmazenamentoFalso : public Armazenamento {
public:
    // --- roteiro: o que cada operação deve responder
    ErroCartao resposta_le_versao = ErroCartao::ArquivoAusente;
    std::string versao_no_cartao;
    ErroCartao resposta_abre = ErroCartao::Nenhum;
    ErroCartao resposta_conclui = ErroCartao::Nenhum;
    ErroCartao resposta_promove = ErroCartao::Nenhum;
    ErroCartao resposta_grava = ErroCartao::Nenhum;
    bool       escrita_falha_em_diante = false;
    /// A partir de qual tentativa o cartão passa a funcionar (1 = sempre).
    unsigned   cura_na_tentativa = 1;

    // --- o que foi observado
    std::vector<std::string> chamadas;
    std::vector<std::uint8_t> escrito;
    std::string versao_gravada;
    std::string promoveu_de, promoveu_para, promoveu_reserva;
    unsigned aberturas = 0;
    unsigned descartes = 0;

    /// Quanto cada operacao do cartao gasta do relogio falso (aponta para o
    /// `agora` da `PausaFalsa`). Zero e o cartao instantaneo de quase toda a
    /// suite; um cartao lento ou degradado leva segundos por operacao.
    std::uint32_t* relogio = nullptr;
    std::uint32_t  custo_abre_ms = 0;
    std::uint32_t  custo_conclui_ms = 0;
    std::uint32_t  custo_promove_ms = 0;
    std::uint32_t  custo_grava_ms = 0;
    void cobra(std::uint32_t ms) { if (relogio != nullptr) { *relogio += ms; } }

    ErroCartao le_arquivo(const char* nome, char* destino, std::size_t cap,
                          std::size_t* lidos, Logger&) override {
        chamadas.push_back(std::string("le:") + nome);
        if (resposta_le_versao != ErroCartao::Nenhum) { return resposta_le_versao; }
        const std::size_t n = versao_no_cartao.size() < cap
                                  ? versao_no_cartao.size() : cap;
        std::memcpy(destino, versao_no_cartao.data(), n);
        destino[n] = '\0';
        *lidos = n;
        return ErroCartao::Nenhum;
    }

    ErroCartao grava_arquivo(const char* nome, const char* conteudo,
                             std::size_t tamanho, Logger&) override {
        chamadas.push_back(std::string("grava:") + nome);
        cobra(custo_grava_ms);
        versao_gravada.assign(conteudo, tamanho);
        return resposta_grava;
    }

    ErroCartao acrescenta_arquivo(const char* nome, const char*, std::size_t,
                                  Logger&) override {
        chamadas.push_back(std::string("acrescenta:") + nome);
        return ErroCartao::Nenhum;
    }

    ErroCartao abre_para_escrita(const char* nome, Logger&) override {
        ++aberturas;
        cobra(custo_abre_ms);
        chamadas.push_back(std::string("abre:") + nome);
        escrito.clear();
        return (aberturas >= cura_na_tentativa) ? ErroCartao::Nenhum
                                                : resposta_abre;
    }

    bool escreve(const std::uint8_t* bytes, std::size_t tamanho) override {
        if (escrita_falha_em_diante && aberturas < cura_na_tentativa) {
            return false;
        }
        escrito.insert(escrito.end(), bytes, bytes + tamanho);
        return true;
    }

    ErroCartao conclui_escrita(Logger&) override {
        chamadas.emplace_back("conclui");
        cobra(custo_conclui_ms);
        return resposta_conclui;
    }

    void descarta_escrita(const char* nome, Logger&) override {
        ++descartes;
        chamadas.push_back(std::string("descarta:") + nome);
    }

    ErroCartao promove(const char* tmp, const char* base, const char* reserva,
                       Logger&) override {
        chamadas.emplace_back("promove");
        cobra(custo_promove_ms);
        promoveu_de = tmp; promoveu_para = base; promoveu_reserva = reserva;
        return resposta_promove;
    }
};

class PausaFalsa : public Pausa {
public:
    std::vector<std::uint32_t> esperas;
    std::uint32_t agora = 0;
    void espera_ms(std::uint32_t ms) override {
        esperas.push_back(ms);
        // O relogio ANDA com a espera: sem isso um teste que verificasse
        // tempo veria o relogio parado apesar de 5 s de pausa.
        agora += ms;
    }
    std::uint32_t agora_ms() override { return agora; }
};

class ConexaoFalsa : public Conexao {
public:
    ErroWifi resposta = ErroWifi::Nenhum;
    unsigned conexoes = 0;
    unsigned desconexoes = 0;
    /// O cao, visto de DENTRO do `conecta`. A associacao do cyw43 bloqueia
    /// por mais do que o prazo e nao tem gancho por dentro: o que se afirma
    /// e que ela roda com o cao solto, e que ele volta depois.
    const teste::CaoDeGuardaFalso* espia = nullptr;
    unsigned suspensoes_ao_conectar = 0;
    unsigned retomadas_ao_conectar = 0;
    unsigned alimentacoes_ao_conectar = 0;

    ErroWifi conecta(const Configuracao&, Logger&) override {
        ++conexoes;
        if (espia != nullptr) {
            alimentacoes_ao_conectar = espia->alimentacoes;
            suspensoes_ao_conectar = espia->suspensoes;
            retomadas_ao_conectar = espia->retomadas;
        }
        return resposta;
    }
    void desconecta(Logger&) override { ++desconexoes; }
};

class BaixadorFalso : public Baixador {
public:
    std::string       corpo_versao = "2026-09-28";
    ResultadoHttp     resposta_versao;
    std::vector<std::uint8_t> corpo_base;
    ResultadoHttp     resposta_base;
    /// A partir de qual download da base ele passa a funcionar.
    unsigned          cura_na_tentativa = 1;
    unsigned          pedidos_versao = 0;
    /// Os caminhos pedidos, na ordem. É onde o segredo do aparelho aparece.
    std::vector<std::string> caminhos;
    unsigned          pedidos_base = 0;
    /// Bytes da base que chegaram a quem recebe, somando as tentativas. E
    /// por aqui que se ve um download que nao parou quando devia.
    std::size_t       entregues_base = 0;

    /// O cao, visto de DENTRO do pedido. A pergunta que isto responde e a
    /// unica que importa: durante um pedido que nao entrega nada, quantas
    /// refeicoes saem? A resposta tem de ser zero.
    const teste::CaoDeGuardaFalso* espia = nullptr;
    unsigned          refeicoes_no_pedido = 0;

    /// Quanto tempo o pedido consome do relogio falso. Zero faz o download
    /// ser instantaneo, que e o que quase toda a suite quer; quem mede
    /// intervalo entre refeicoes precisa de um pedido que custe tempo.
    PausaFalsa*       relogio = nullptr;
    std::uint32_t     gasta_ms = 0;

    ResultadoHttp baixa(const Url& url, AoReceber ao_receber, void* contexto,
                        Logger&, std::uint32_t) override {
        caminhos.emplace_back(url.caminho);
        const unsigned antes = espia != nullptr ? espia->alimentacoes : 0;
        const auto r = responde(url, ao_receber, contexto);
        if (espia != nullptr) {
            refeicoes_no_pedido = espia->alimentacoes - antes;
        }
        return r;
    }

private:
    ResultadoHttp responde(const Url& url, AoReceber ao_receber,
                           void* contexto) {
        if (relogio != nullptr) { relogio->agora += gasta_ms; }
        const bool e_versao = std::strstr(url.caminho, "versao") != nullptr;
        if (e_versao) {
            ++pedidos_versao;
            if (!resposta_versao.ok()) { return resposta_versao; }
            ao_receber(contexto,
                       reinterpret_cast<const std::uint8_t*>(corpo_versao.data()),
                       corpo_versao.size());
            return ResultadoHttp{};
        }
        ++pedidos_base;
        // Entrega em pedaços, como a rede faz: exercita o caminho de fluxo.
        const std::size_t passo = 64;
        for (std::size_t i = 0; i < corpo_base.size(); i += passo) {
            const std::size_t n = (i + passo <= corpo_base.size())
                                      ? passo : corpo_base.size() - i;
            entregues_base += n;
            if (!ao_receber(contexto, corpo_base.data() + i, n)) {
                // O que o `ClienteTls` faz: aborta a conexao e devolve
                // `Interrompida`, com o status que tinha lido.
                return ResultadoHttp{ErroHttp::Interrompida, 200, i + n};
            }
        }
        if (pedidos_base < cura_na_tentativa) { return resposta_base; }
        return ResultadoHttp{};
    }
};

/// O preparo da sessao (SNTP no aparelho): custa tempo, como la.
class PreparoFalso : public PreparoDeSessao {
public:
    std::uint32_t* relogio = nullptr;
    std::uint32_t  custo_ms = 0;
    void apos_conectar(Logger&) override {
        if (relogio != nullptr) { *relogio += custo_ms; }
    }
};

// ================================================================== cenário

struct Bancada {
    ArmazenamentoFalso cartao;
    ConexaoFalsa       rede;
    BaixadorFalso      http;
    PausaFalsa         pausa;
    teste::LoggerMock  log;
    Configuracao       cfg;
    /// O que o boot diria: a base em uso veio do `radares.bin`. E o caso
    /// sao, e e o de quase toda a suite; os testes do F3 o desligam.
    bool               vigente_em_uso = true;
    teste::CaoCronometrado cao{pausa};
    PreparoFalso       preparo;

    Bancada() {
        http.espia = &cao;
        http.relogio = &pausa;
        rede.espia = &cao;
        std::snprintf(cfg.url_versao, sizeof cfg.url_versao,
                      "http://exemplo/radares.versao");
        std::snprintf(cfg.url_base, sizeof cfg.url_base,
                      "http://exemplo/radares.bin");
        std::snprintf(cfg.redes[0].ssid, sizeof cfg.redes[0].ssid, "rede");
        std::snprintf(cfg.redes[0].senha, sizeof cfg.redes[0].senha, "segredo");
        cfg.n_redes = 1;
        http.corpo_base = base_valida();
    }

    ResultadoOta roda() {
        cartao.relogio = &pausa.agora;
        preparo.relogio = &pausa.agora;
        AtualizadorOta ota(cartao, rede, http, pausa, nullptr, &preparo, &cao);
        ota.informa_vigente_em_uso(vigente_em_uso);
        const auto r = ota.executa(cfg, log);
        cao.fecha();
        return r;
    }

    bool chamou(const std::string& o) const {
        for (const auto& c : cartao.chamadas) { if (c == o) { return true; } }
        return false;
    }
};

// =============================================================== pré-condições

TEST(AtualizadorOta, sem_urls_nao_liga_o_radio) {
    Bancada b;
    b.cfg.url_base[0] = '\0';
    EXPECT_EQ(b.roda(), ResultadoOta::SemConfiguracao);
    EXPECT_EQ(b.rede.conexoes, 0U) << "nao deve subir o enlace para nada";
}

TEST(AtualizadorOta, url_inanalisavel_e_recusada_antes_de_conectar) {
    Bancada b;
    std::snprintf(b.cfg.url_base, sizeof b.cfg.url_base, "isto nao e uma url");
    EXPECT_EQ(b.roda(), ResultadoOta::SemConfiguracao);
    EXPECT_EQ(b.rede.conexoes, 0U);
}

TEST(AtualizadorOta, falha_de_rede_encerra_sem_tentar_baixar) {
    Bancada b;
    b.rede.resposta = ErroWifi::NenhumaRedeVisivel;
    EXPECT_EQ(b.roda(), ResultadoOta::FalhaDeRede);
    EXPECT_EQ(b.http.pedidos_versao, 0U);
}

// ================================================================== versão

TEST(AtualizadorOta, versao_igual_com_a_vigente_em_uso_nao_baixa_a_base) {
    // O caso sao: o boot carregou o `radares.bin`, e a versao do servidor e
    // a que ele descreve. Baixar de novo seriam 214 KB a toa.
    Bancada b;
    b.vigente_em_uso = true;
    b.cartao.resposta_le_versao = ErroCartao::Nenhum;
    b.cartao.versao_no_cartao = "2026-09-28";
    b.http.corpo_versao = "2026-09-28";
    EXPECT_EQ(b.roda(), ResultadoOta::JaEstavaEmDia);
    EXPECT_EQ(b.http.pedidos_base, 0U) << "baixou 214 KB a toa";
    EXPECT_EQ(b.rede.desconexoes, 1U);
}

// --- F3: a versao local so vale se descreve a base em uso ---
//
// O `versao.txt` descreve o `radares.bin`. Quando o boot recusa o `.bin` e
// cai na reserva -- ou nao acha base nenhuma --, a versao continua la,
// igual a do servidor, e o OTA respondia JA ESTA EM DIA para sempre: o menu
// nao tinha como consertar o aparelho.

TEST(AtualizadorOta, versao_igual_rodando_pela_reserva_baixa_de_novo) {
    Bancada b;
    b.vigente_em_uso = false;
    b.cartao.resposta_le_versao = ErroCartao::Nenhum;
    b.cartao.versao_no_cartao = "2026-09-28";
    b.http.corpo_versao = "2026-09-28";
    EXPECT_EQ(b.roda(), ResultadoOta::Atualizada)
        << "o aparelho segue pela reserva e o menu nao conserta";
    EXPECT_EQ(b.http.pedidos_base, 1U);
    EXPECT_EQ(b.cartao.promoveu_para, kArquivoBase);
}

TEST(AtualizadorOta, sem_saber_de_onde_veio_a_base_versao_igual_baixa) {
    // Quem nao informa nada nao ganha a economia: na duvida, o desperdicio
    // de rede e o modo de falha certo -- o mesmo da versao gravada por
    // ultimo.
    Bancada b;
    b.cartao.resposta_le_versao = ErroCartao::Nenhum;
    b.cartao.versao_no_cartao = "2026-09-28";
    b.http.corpo_versao = "2026-09-28";
    AtualizadorOta ota(b.cartao, b.rede, b.http, b.pausa);
    EXPECT_EQ(ota.executa(b.cfg, b.log), ResultadoOta::Atualizada);
    EXPECT_EQ(b.http.pedidos_base, 1U);
}

TEST(AtualizadorOta, depois_de_atualizar_a_versao_igual_volta_a_bastar) {
    // A base que o OTA acabou de promover e a vigente: o clique seguinte
    // nao pode rebaixa-la so porque o boot tinha caido na reserva.
    Bancada b;
    b.cartao.resposta_le_versao = ErroCartao::Nenhum;
    b.cartao.versao_no_cartao = "2026-09-28";
    b.http.corpo_versao = "2026-09-28";
    AtualizadorOta ota(b.cartao, b.rede, b.http, b.pausa);
    ota.informa_vigente_em_uso(false);
    ASSERT_EQ(ota.executa(b.cfg, b.log), ResultadoOta::Atualizada);

    b.cartao.versao_no_cartao = b.cartao.versao_gravada;
    EXPECT_EQ(ota.executa(b.cfg, b.log), ResultadoOta::JaEstavaEmDia);
    EXPECT_EQ(b.http.pedidos_base, 1U) << "rebaixou a base que acabou de gravar";
}

TEST(AtualizadorOta, falha_ao_rebaixar_pela_reserva_nao_finge_em_dia) {
    // Se o download que conserta falhar, a proxima tentativa tem de
    // insistir: a base em uso continua sendo a reserva.
    Bancada b;
    b.cartao.resposta_le_versao = ErroCartao::Nenhum;
    b.cartao.versao_no_cartao = "2026-09-28";
    b.http.corpo_versao = "2026-09-28";
    b.http.resposta_base = ResultadoHttp{ErroHttp::Interrompida, 0, 0};
    b.http.cura_na_tentativa = kTentativas + 1;
    AtualizadorOta ota(b.cartao, b.rede, b.http, b.pausa);
    ota.informa_vigente_em_uso(false);
    ASSERT_EQ(ota.executa(b.cfg, b.log), ResultadoOta::FalhaAoBaixar);

    EXPECT_EQ(ota.executa(b.cfg, b.log), ResultadoOta::Atualizada);
}

TEST(AtualizadorOta, espaco_em_volta_da_versao_nao_conta_como_diferenca) {
    // O servidor devolve a linha com \r\n; o cartao guarda sem. Se a
    // comparacao fosse crua, baixaria a base inteira a cada clique.
    Bancada b;
    b.cartao.resposta_le_versao = ErroCartao::Nenhum;
    b.cartao.versao_no_cartao = "2026-09-28";
    b.http.corpo_versao = "2026-09-28\r\n";
    EXPECT_EQ(b.roda(), ResultadoOta::JaEstavaEmDia);
}

TEST(AtualizadorOta, versao_vazia_do_servidor_e_falha_e_nao_atualizacao) {
    Bancada b;
    b.http.corpo_versao = "   \r\n";
    EXPECT_EQ(b.roda(), ResultadoOta::FalhaAoConsultar);
    EXPECT_EQ(b.http.pedidos_base, 0U);
}

TEST(AtualizadorOta, versao_com_quebra_de_linha_nao_injeta_no_log) {
    // A versao vem do servidor e vai para o log -- e o log e lido depois,
    // linha a linha, para saber o que aconteceu. Uma quebra de linha nela
    // escreve linhas inteiras que ninguem registrou. Ela tambem vai para o
    // `versao.txt` e volta na comparacao do clique seguinte.
    Bancada b;
    b.http.corpo_versao = "2026-09-28\nota: base gravada em 'radares.bin'";
    EXPECT_EQ(b.roda(), ResultadoOta::FalhaAoConsultar);
    EXPECT_EQ(b.http.pedidos_base, 0U);
    for (const auto& e : b.log.entradas()) {
        EXPECT_EQ(e.mensagem.find('\n'), std::string::npos) << e.mensagem;
    }
}

TEST(AtualizadorOta, versao_com_caractere_de_controle_e_recusada) {
    Bancada b;
    b.http.corpo_versao = "2026-09-\x1b[2J28";
    EXPECT_EQ(b.roda(), ResultadoOta::FalhaAoConsultar);
    EXPECT_EQ(b.http.pedidos_base, 0U);
}

TEST(AtualizadorOta, versao_comum_do_servidor_continua_passando) {
    // O par do teste acima: uma guarda que recusasse tudo passaria nele.
    for (const char* v : {"2026-09-28", "v1.2.3", "a7f3c9e",
                          "2026-09-28T12:00:00Z"}) {
        Bancada b;
        b.http.corpo_versao = v;
        EXPECT_EQ(b.roda(), ResultadoOta::Atualizada) << v;
    }
}

TEST(AtualizadorOta, a_versao_na_forma_que_o_servidor_REALMENTE_manda_passa) {
    // 🔴 A regressao da v0.2.26, que deixou a base sem atualizar no carro.
    //
    // A linha vem de `servidor.versao_de`, em Python: `crc32:... pontos:...
    // formato:... data:...`, com ESPACOS e um `\n` no fim (54 bytes no log do
    // aparelho). A validacao de entrada reaproveitava `seguro_para_cabecalho`,
    // que foi escrito para token e URL e RECUSA o espaco de proposito -- e
    // por isso recusava TODA versao legitima do servidor:
    //   ota: a versao do servidor tem caractere que nao se registra; recusada
    //
    // Passou por tres revisoes e por todos os testes porque as amostras do
    // par acima ("v1.2.3", "a7f3c9e") foram inventadas, e nenhuma tinha
    // espaco. O contrato entre o servidor e o firmware so se prova com o dado
    // de verdade; a forma exata esta no teste do lado do servidor
    // (`test_a_versao_anunciada_e_ascii_imprimivel`).
    const char* real = "crc32:34e87e07 pontos:18479 formato:2 data:2026-11-20";
    {
        Bancada b;
        b.http.corpo_versao = real;
        EXPECT_EQ(b.roda(), ResultadoOta::Atualizada);
        EXPECT_EQ(b.cartao.versao_gravada, real);
    }
    {   // E com o \n que o servidor manda, aparado antes de comparar e gravar.
        Bancada b;
        b.http.corpo_versao = std::string(real) + "\n";
        EXPECT_EQ(b.roda(), ResultadoOta::Atualizada);
        EXPECT_EQ(b.cartao.versao_gravada, real) << "gravou o \\n junto";
    }
}

TEST(AtualizadorOta, versao_com_controle_no_meio_segue_recusada) {
    // O outro lado: o espaco passa, mas o que a validacao existe para barrar
    // -- quebra de linha forjando entrada no log que sobe pela remessa, ou
    // sequencia de escape -- continua barrado, mesmo no meio de uma linha
    // que de resto parece a real.
    for (const char* v : {"crc32:34e87e07 pontos:18479\r\nERRO: forjado",
                          "crc32:34e87e07\tpontos:18479",
                          "crc32:34e87e07 pontos:18479\x1b[2J",
                          "crc32:34e87e07 pontos:18479 \xc3\xa9"}) {
        Bancada b;
        b.http.corpo_versao = v;
        EXPECT_EQ(b.roda(), ResultadoOta::FalhaAoConsultar) << v;
        EXPECT_EQ(b.http.pedidos_base, 0U) << v;
    }
}

TEST(AtualizadorOta, com_a_base_real_do_servidor_o_ota_completa_de_ponta_a_ponta) {
    // Roda so quando ha uma base completa configurada (-DCORUJA_BASE_REAL=...),
    // como o `BaseCompleta`. A linha de versao sai do CABECALHO do proprio
    // arquivo, que e como o servidor a deriva (`servidor.versao_de`), e o
    // download passa por TODO o caminho: consulta, verificacao em fluxo,
    // gravacao, troca atomica e versao.
    //
    // Existe porque na v0.2.26 o aparelho nunca passou da consulta de versao,
    // e o que vem depois -- 221 KB, reescrito naquela serie -- nunca foi
    // exercitado com dado de verdade.
    if (std::string(CORUJA_BASE_REAL).empty()) {
        GTEST_SKIP() << "sem base real; passe -DCORUJA_BASE_REAL=/caminho";
    }
    std::ifstream f(CORUJA_BASE_REAL, std::ios::binary);
    std::vector<std::uint8_t> real((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
    ASSERT_GT(real.size(), 20U) << "base real ilegivel";
    auto u16 = [&](std::size_t o) { return real[o] | (real[o + 1] << 8); };
    auto u32 = [&](std::size_t o) {
        return static_cast<std::uint32_t>(real[o]) | (real[o + 1] << 8) |
               (real[o + 2] << 16) | (static_cast<std::uint32_t>(real[o + 3]) << 24);
    };
    char linha[96];
    std::snprintf(linha, sizeof linha,
                  "crc32:%08x pontos:%u formato:%u data:%04u-%02u-%02u",
                  u32(12), u32(8), u16(4), u16(16), real[18], real[19]);

    Bancada b;
    b.cartao.versao_no_cartao = "crc32:2a2129db pontos:18469 formato:2 data:2026-11-11";
    b.http.corpo_versao = std::string(linha) + "\n";
    b.http.corpo_base = real;
    EXPECT_EQ(b.roda(), ResultadoOta::Atualizada) << linha;
    EXPECT_EQ(b.cartao.escrito, real) << "gravou algo diferente do que chegou";
    EXPECT_EQ(b.cartao.versao_gravada, linha);
    EXPECT_EQ(b.http.pedidos_base, 1U);
}

TEST(AtualizadorOta, consulta_que_falha_nao_baixa) {
    Bancada b;
    b.http.resposta_versao = ResultadoHttp{ErroHttp::TempoEsgotado, 0, 0};
    EXPECT_EQ(b.roda(), ResultadoOta::FalhaAoConsultar);
    EXPECT_EQ(b.http.pedidos_base, 0U);
    EXPECT_EQ(b.rede.desconexoes, 1U) << "tem de baixar o radio mesmo falhando";
}

// ======================================================= caminho de sucesso

TEST(AtualizadorOta, sucesso_promove_e_so_entao_grava_a_versao) {
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoOta::Atualizada);

    EXPECT_EQ(b.cartao.promoveu_de, kArquivoTmp);
    EXPECT_EQ(b.cartao.promoveu_para, kArquivoBase);
    EXPECT_EQ(b.cartao.promoveu_reserva, kArquivoBak);
    EXPECT_EQ(b.cartao.versao_gravada, "2026-09-28");
    EXPECT_EQ(b.cartao.descartes, 0U);
    EXPECT_EQ(b.rede.desconexoes, 1U);

    // A ORDEM e o coracao do RF05.2: a versao e o ultimo passo. Grava-la
    // antes da troca deixaria o cartao afirmando ter uma base que nao tem,
    // e o proximo boot carregaria a antiga achando que esta em dia.
    const auto& c = b.cartao.chamadas;
    const auto pos = [&](const std::string& o) {
        for (std::size_t i = 0; i < c.size(); ++i) { if (c[i] == o) { return i; } }
        return c.size();
    };
    EXPECT_LT(pos("abre:radares.tmp"), pos("conclui"));
    EXPECT_LT(pos("conclui"), pos("promove"));
    EXPECT_LT(pos("promove"), pos("grava:versao.txt"));
}

TEST(AtualizadorOta, o_que_chegou_pela_rede_foi_o_que_se_escreveu) {
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoOta::Atualizada);
    EXPECT_EQ(b.cartao.escrito, b.http.corpo_base);
}

TEST(AtualizadorOta, primeira_atualizacao_sem_versao_no_cartao) {
    Bancada b;
    b.cartao.resposta_le_versao = ErroCartao::ArquivoAusente;
    EXPECT_EQ(b.roda(), ResultadoOta::Atualizada);
}

TEST(AtualizadorOta, versao_local_ilegivel_rebaixa_em_vez_de_desistir) {
    Bancada b;
    b.cartao.resposta_le_versao = ErroCartao::FalhaDeLeitura;
    EXPECT_EQ(b.roda(), ResultadoOta::Atualizada);
}

TEST(AtualizadorOta, base_gravada_e_versao_nao_ainda_e_sucesso) {
    // Perder so a versao e recuperavel: rebaixa no proximo clique. Tratar
    // como falha faria descartar uma base integra ja gravada.
    Bancada b;
    b.cartao.resposta_grava = ErroCartao::FalhaDeEscrita;
    EXPECT_EQ(b.roda(), ResultadoOta::Atualizada);
}

// ============================================== retentativas do RF05.2

TEST(AtualizadorOta, tres_tentativas_com_espera_entre_elas) {
    Bancada b;
    b.http.resposta_base = ResultadoHttp{ErroHttp::Interrompida, 0, 0};
    b.http.cura_na_tentativa = 99;   // nunca cura

    EXPECT_EQ(b.roda(), ResultadoOta::FalhaAoBaixar);
    EXPECT_EQ(b.http.pedidos_base, kTentativas);
    // Espera ENTRE as tentativas: duas pausas para tres tentativas, nao tres.
    ASSERT_EQ(b.pausa.esperas.size(), kTentativas - 1);
    for (const auto ms : b.pausa.esperas) {
        EXPECT_EQ(ms, kEsperaEntreTentativasMs);
    }
}

TEST(AtualizadorOta, cada_tentativa_frustrada_descarta_o_temporario) {
    // Sem isto ficaria um radares.tmp meio escrito ocupando o cartao.
    Bancada b;
    b.http.resposta_base = ResultadoHttp{ErroHttp::Interrompida, 0, 0};
    b.http.cura_na_tentativa = 99;
    b.roda();
    EXPECT_EQ(b.cartao.descartes, kTentativas);
}

TEST(AtualizadorOta, recupera_na_terceira_tentativa) {
    Bancada b;
    b.http.resposta_base = ResultadoHttp{ErroHttp::Interrompida, 0, 0};
    b.http.cura_na_tentativa = 3;
    EXPECT_EQ(b.roda(), ResultadoOta::Atualizada);
    EXPECT_EQ(b.http.pedidos_base, 3U);
    EXPECT_EQ(b.cartao.descartes, 2U);
    EXPECT_EQ(b.pausa.esperas.size(), 2U);
}

TEST(AtualizadorOta, a_base_vigente_sobrevive_a_todas_as_falhas) {
    // A promessa do RF05.2: falhar nao pode piorar o que ja existe.
    Bancada b;
    b.http.resposta_base = ResultadoHttp{ErroHttp::Interrompida, 0, 0};
    b.http.cura_na_tentativa = 99;
    b.roda();
    EXPECT_FALSE(b.chamou("promove")) << "trocou a base apos falhar";
    EXPECT_TRUE(b.cartao.versao_gravada.empty()) << "mexeu na versao apos falhar";
}

// ================================================= integridade e escrita

TEST(AtualizadorOta, base_corrompida_e_recusada_e_nao_promovida) {
    Bancada b;
    b.http.corpo_base = base_valida();
    b.http.corpo_base[20] ^= 0xFF;   // estraga um registro, o CRC nao fecha
    EXPECT_EQ(b.roda(), ResultadoOta::BaseRecusada);
    EXPECT_FALSE(b.chamou("promove"));
    EXPECT_EQ(b.cartao.descartes, kTentativas);
}

TEST(AtualizadorOta, base_truncada_e_recusada) {
    Bancada b;
    b.http.corpo_base = base_valida();
    b.http.corpo_base.resize(b.http.corpo_base.size() - 4);
    EXPECT_EQ(b.roda(), ResultadoOta::BaseRecusada);
    EXPECT_FALSE(b.chamou("promove"));
}

TEST(AtualizadorOta, cartao_que_nao_abre_e_falha_de_gravacao) {
    Bancada b;
    b.cartao.resposta_abre = ErroCartao::SemCartaoLegivel;
    b.cartao.cura_na_tentativa = 99;
    EXPECT_EQ(b.roda(), ResultadoOta::FalhaAoGravar);
    EXPECT_EQ(b.http.pedidos_base, 0U) << "nao adianta baixar sem onde por";
}

TEST(AtualizadorOta, cartao_que_enche_no_meio_do_download) {
    // Impraticavel de provocar na bancada, trivial aqui -- e era exatamente
    // este caminho que nao tinha como ser verificado antes das interfaces.
    Bancada b;
    b.cartao.escrita_falha_em_diante = true;
    b.cartao.cura_na_tentativa = 99;
    EXPECT_EQ(b.roda(), ResultadoOta::FalhaAoGravar);
    EXPECT_FALSE(b.chamou("promove"));
    EXPECT_EQ(b.cartao.descartes, kTentativas);
}

TEST(AtualizadorOta, escrita_que_falha_para_o_download_na_hora) {
    // Continuar baixando depois de o cartao recusar era ate 120 s de
    // aparelho bloqueado -- sem GPS, sem alerta -- para nada, a cada
    // tentativa.
    Bancada b;
    b.http.corpo_base = base_valida(200);   // 2420 B, 38 pedacos de 64
    b.cartao.escrita_falha_em_diante = true;
    b.cartao.cura_na_tentativa = 99;
    EXPECT_EQ(b.roda(), ResultadoOta::FalhaAoGravar);
    EXPECT_EQ(b.http.entregues_base, kTentativas * 64U)
        << "seguiu baixando depois de o cartao recusar";
}

// --- o teto do que vai para o cartao ---

/// Um cabecalho v2 que promete `pontos` registros, sem os registros.
std::vector<std::uint8_t> cabecalho_que_promete(std::uint32_t pontos) {
    auto base = base_valida(1);
    base.resize(kTamCabecalho);
    base[8] = static_cast<std::uint8_t>(pontos);
    base[9] = static_cast<std::uint8_t>(pontos >> 8);
    base[10] = static_cast<std::uint8_t>(pontos >> 16);
    base[11] = static_cast<std::uint8_t>(pontos >> 24);
    return base;
}

TEST(AtualizadorOta, nada_alem_do_que_a_placa_comporta_chega_ao_cartao) {
    // Um servidor errado -- ou hostil -- que mande mais do que cabe na
    // placa enchia o cartao ate onde ele deixasse: o teto so era conferido
    // no fim, pelo verificador, com tudo ja gravado.
    Bancada b;
    b.http.corpo_base = cabecalho_que_promete(kCapacidadeFirmware + 1);
    b.http.corpo_base.resize(kTetoBase + 4096, 0x55);
    EXPECT_EQ(b.roda(), ResultadoOta::BaseRecusada);
    EXPECT_LE(b.cartao.escrito.size(), kTetoBase);
    EXPECT_FALSE(b.chamou("promove"));
    EXPECT_EQ(b.http.pedidos_base, 1U)
        << "o servidor mandaria o mesmo arquivo grande de novo";
    EXPECT_EQ(b.cartao.descartes, 1U);
}

TEST(AtualizadorOta, a_maior_base_que_a_placa_comporta_passa_pelo_teto) {
    // O teto e a capacidade, e nao menos: a base cheia tem de caber.
    Bancada b;
    b.http.corpo_base = base_valida(kCapacidadeFirmware);
    ASSERT_EQ(b.http.corpo_base.size(), kTetoBase);
    EXPECT_EQ(b.roda(), ResultadoOta::Atualizada);
    EXPECT_EQ(b.cartao.escrito.size(), kTetoBase);
}

TEST(AtualizadorOta, falha_ao_concluir_a_escrita_descarta) {
    Bancada b;
    b.cartao.resposta_conclui = ErroCartao::FalhaDeEscrita;
    EXPECT_EQ(b.roda(), ResultadoOta::FalhaAoGravar);
    EXPECT_FALSE(b.chamou("promove"));
}

TEST(AtualizadorOta, falha_na_troca_atomica_nao_tenta_de_novo) {
    // Renomeacao que falha nao melhora na tentativa seguinte, e repetir
    // arriscaria o estado do cartao. Sai na hora.
    Bancada b;
    b.cartao.resposta_promove = ErroCartao::FalhaDeRenomeacao;
    EXPECT_EQ(b.roda(), ResultadoOta::FalhaAoGravar);
    EXPECT_EQ(b.http.pedidos_base, 1U) << "insistiu numa falha que nao se cura";
    EXPECT_EQ(b.rede.desconexoes, 1U);
}

// ================================================== o erro que nao muda
//
// Ate 17/11/2026 o OTA repetia toda falha de download tres vezes, com 5 s
// entre elas, e a tela dizia DOWNLOAD INCOMPLETO -- inclusive para um 401
// (token errado), um 404 (`url_base` errada) ou um certificado recusado
// (relogio sem hora). Nada disso muda em cinco segundos. A leitura e a da
// remessa: `classifica_falha`.

TEST(ErroPermanenteOta, recusa_na_consulta_diz_que_o_servidor_recusou) {
    Bancada b;
    b.http.resposta_versao = ResultadoHttp{ErroHttp::StatusNaoOk, 401, 0};
    EXPECT_EQ(b.roda(), ResultadoOta::ServidorRecusou)
        << "um 401 nao e SERVIDOR NAO RESPONDE: ele respondeu";
    EXPECT_EQ(b.http.pedidos_base, 0U);
}

TEST(ErroPermanenteOta, certificado_recusado_na_consulta_diz_o_motivo) {
    Bancada b;
    b.http.resposta_versao = ResultadoHttp{ErroHttp::CertificadoRecusado, 0, 0};
    EXPECT_EQ(b.roda(), ResultadoOta::CertificadoRecusado);
    EXPECT_EQ(b.http.pedidos_base, 0U);
}

TEST(ErroPermanenteOta, consulta_sem_resposta_ou_com_erro_de_momento_nao_muda) {
    for (const auto r : {ResultadoHttp{ErroHttp::TempoEsgotado, 0, 0},
                         ResultadoHttp{ErroHttp::StatusNaoOk, 503, 0}}) {
        Bancada b;
        b.http.resposta_versao = r;
        EXPECT_EQ(b.roda(), ResultadoOta::FalhaAoConsultar) << r.status;
    }
}

TEST(ErroPermanenteOta, download_recusado_nao_e_repetido) {
    for (const std::uint32_t status : {401U, 403U, 404U}) {
        Bancada b;
        b.http.resposta_base = ResultadoHttp{ErroHttp::StatusNaoOk, status, 0};
        b.http.cura_na_tentativa = 99;
        EXPECT_EQ(b.roda(), ResultadoOta::ServidorRecusou) << status;
        EXPECT_EQ(b.http.pedidos_base, 1U) << status;
        EXPECT_TRUE(b.pausa.esperas.empty()) << status;
        EXPECT_EQ(b.cartao.descartes, 1U) << "o temporario ficou no cartao";
        EXPECT_FALSE(b.chamou("promove"));
    }
}

TEST(ErroPermanenteOta, certificado_recusado_no_download_nao_e_repetido) {
    Bancada b;
    b.http.resposta_base = ResultadoHttp{ErroHttp::CertificadoRecusado, 0, 0};
    b.http.cura_na_tentativa = 99;
    EXPECT_EQ(b.roda(), ResultadoOta::CertificadoRecusado);
    EXPECT_EQ(b.http.pedidos_base, 1U);
}

TEST(ErroPermanenteOta, recusa_que_chega_com_a_conexao_caindo_vale_pelo_status) {
    // O servidor recusa sem ler o resto e fecha; o RST chega atras da
    // resposta, e o cliente devolve `Interrompida` com o 401 que leu.
    Bancada b;
    b.http.resposta_base = ResultadoHttp{ErroHttp::Interrompida, 401, 0};
    b.http.cura_na_tentativa = 99;
    EXPECT_EQ(b.roda(), ResultadoOta::ServidorRecusou);
    EXPECT_EQ(b.http.pedidos_base, 1U);
}

TEST(ErroPermanenteOta, erro_de_momento_do_servidor_continua_sendo_repetido) {
    Bancada b;
    b.http.resposta_base = ResultadoHttp{ErroHttp::StatusNaoOk, 503, 0};
    b.http.cura_na_tentativa = 99;
    EXPECT_EQ(b.roda(), ResultadoOta::FalhaAoBaixar);
    EXPECT_EQ(b.http.pedidos_base, kTentativas);
}

TEST(ErroPermanenteOta, pedido_que_o_cliente_nem_monta_acusa_a_configuracao) {
    // Cabecalho que nao coube, ou token com caractere invalido: o pedido nem
    // saiu, e repetir monta o mesmo pedido.
    Bancada b;
    b.http.resposta_base = ResultadoHttp{ErroHttp::UrlInvalida, 0, 0};
    b.http.cura_na_tentativa = 99;
    EXPECT_EQ(b.roda(), ResultadoOta::SemConfiguracao);
    EXPECT_EQ(b.http.pedidos_base, 1U);
}

TEST(AtualizadorOta, o_radio_desce_em_todos_os_desfechos) {
    for (int caso = 0; caso < 4; ++caso) {
        Bancada b;
        if (caso == 1) { b.http.resposta_versao = ResultadoHttp{ErroHttp::NaoIniciou, 0, 0}; }
        if (caso == 2) { b.http.resposta_base = ResultadoHttp{ErroHttp::Interrompida, 0, 0};
                         b.http.cura_na_tentativa = 99; }
        if (caso == 3) { b.cartao.resposta_promove = ErroCartao::FalhaDeRenomeacao; }
        b.roda();
        EXPECT_EQ(b.rede.desconexoes, 1U) << "caso " << caso
            << ": deixou o radio ligado, gastando corrente e sem alerta";
    }
}

// ------------------------------------------------ o que a tela ve

/// Anota a sequencia de fases e o ultimo progresso.
class ObservadorEspiao : public ObservadorOta {
public:
    std::vector<FaseOta> fases;
    std::vector<unsigned> tentativas;
    std::size_t ultimos_recebidos = 0;
    std::size_t ultimo_total = 0;
    unsigned    avisos_de_progresso = 0;

    void fase(FaseOta f, unsigned t) override {
        fases.push_back(f);
        tentativas.push_back(t);
    }
    void progresso(std::size_t r, std::size_t total) override {
        ultimos_recebidos = r;
        ultimo_total = total;
        ++avisos_de_progresso;
    }
    bool tem(FaseOta f) const {
        for (auto x : fases) { if (x == f) { return true; } }
        return false;
    }
    int posicao_de(FaseOta f) const {
        for (std::size_t i = 0; i < fases.size(); ++i) {
            if (fases[i] == f) { return static_cast<int>(i); }
        }
        return -1;
    }
};

TEST(AtualizadorOta, anuncia_as_fases_na_ordem_da_execucao) {
    // A ordem importa para a tela: cada fase falha por motivo diferente, e
    // um "erro na atualizacao" genérico obrigaria a abrir o log para saber
    // o que tentar.
    Bancada b;
    ObservadorEspiao obs;
    b.cartao.versao_no_cartao = "antiga";
    b.cartao.resposta_le_versao = ErroCartao::Nenhum;
    AtualizadorOta ota(b.cartao, b.rede, b.http, b.pausa, &obs);
    ASSERT_EQ(ota.executa(b.cfg, b.log), ResultadoOta::Atualizada);

    EXPECT_LT(obs.posicao_de(FaseOta::Conectando),
              obs.posicao_de(FaseOta::Consultando));
    EXPECT_LT(obs.posicao_de(FaseOta::Consultando),
              obs.posicao_de(FaseOta::Baixando));
    EXPECT_LT(obs.posicao_de(FaseOta::Baixando),
              obs.posicao_de(FaseOta::Verificando));
    EXPECT_LT(obs.posicao_de(FaseOta::Verificando),
              obs.posicao_de(FaseOta::Gravando));
    EXPECT_LT(obs.posicao_de(FaseOta::Gravando),
              obs.posicao_de(FaseOta::Concluida));
    EXPECT_FALSE(obs.tem(FaseOta::Falhou));
}

TEST(AtualizadorOta, falha_de_rede_avisa_e_nao_finge_progresso) {
    Bancada b;
    ObservadorEspiao obs;
    b.rede.resposta = ErroWifi::NenhumaRedeVisivel;
    AtualizadorOta ota(b.cartao, b.rede, b.http, b.pausa, &obs);
    ota.executa(b.cfg, b.log);

    EXPECT_TRUE(obs.tem(FaseOta::Conectando));
    EXPECT_TRUE(obs.tem(FaseOta::Falhou));
    EXPECT_FALSE(obs.tem(FaseOta::Baixando)) << "anunciou download sem rede";
    EXPECT_EQ(obs.avisos_de_progresso, 0U);
}

TEST(AtualizadorOta, ja_em_dia_nao_anuncia_download) {
    // Fim feliz que nao baixa nada. A tela tem de dizer isso, e nao
    // "concluida" -- o usuario clicou esperando uma atualizacao.
    Bancada b;
    ObservadorEspiao obs;
    b.cartao.versao_no_cartao = "v9";
    b.cartao.resposta_le_versao = ErroCartao::Nenhum;
    b.http.corpo_versao = "v9";
    AtualizadorOta ota(b.cartao, b.rede, b.http, b.pausa, &obs);
    ota.informa_vigente_em_uso(true);
    ASSERT_EQ(ota.executa(b.cfg, b.log), ResultadoOta::JaEstavaEmDia);

    EXPECT_TRUE(obs.tem(FaseOta::JaEmDia));
    EXPECT_FALSE(obs.tem(FaseOta::Baixando));
    EXPECT_FALSE(obs.tem(FaseOta::Concluida))
        << "'em dia' e 'atualizada' sao coisas diferentes para quem clicou";
}

TEST(AtualizadorOta, o_progresso_chega_com_total_depois_do_cabecalho) {
    // O total nao existe antes do cabecalho: e ele que diz quantos pontos
    // vem. Uma barra de progresso sem denominador nao se desenha.
    Bancada b;
    ObservadorEspiao obs;
    b.cartao.versao_no_cartao = "antiga";
    b.cartao.resposta_le_versao = ErroCartao::Nenhum;
    AtualizadorOta ota(b.cartao, b.rede, b.http, b.pausa, &obs);
    ASSERT_EQ(ota.executa(b.cfg, b.log), ResultadoOta::Atualizada);

    EXPECT_GT(obs.avisos_de_progresso, 0U) << "ninguem avisou o progresso";
    EXPECT_GT(obs.ultimo_total, 0U) << "o total ficou desconhecido";
    EXPECT_EQ(obs.ultimos_recebidos, obs.ultimo_total)
        << "o download terminou sem alcancar o total anunciado";
}

TEST(AtualizadorOta, contagem_absurda_no_cabecalho_nao_estoura_o_total) {
    // `kTamCabecalho + n_pontos * kTamRegistro` com o `n_pontos` que o
    // servidor quiser: no Pico, `size_t` tem 32 bits, e 0xFFFFFFFF pontos
    // davam um total que deu a volta. A tela recebia lixo como denominador.
    Bancada b;
    ObservadorEspiao obs;
    b.http.corpo_base = cabecalho_que_promete(0xFFFFFFFFU);
    // Um registro depois do cabecalho: o verificador so o da por lido
    // quando chega o primeiro byte de dados.
    b.http.corpo_base.resize(kTamCabecalho + kTamRegistro, 0);
    AtualizadorOta ota(b.cartao, b.rede, b.http, b.pausa, &obs);
    ota.executa(b.cfg, b.log);
    ASSERT_GT(obs.avisos_de_progresso, 0U);
    EXPECT_LE(obs.ultimo_total, kTetoBase);
}

TEST(AtualizadorOta, o_progresso_de_uma_base_sem_data_chega_ao_fim) {
    // O formato 1 tem cabecalho de 16 B, e nao 20: com o tamanho fixo, a
    // barra parava 4 B antes do total e nunca enchia.
    Bancada b;
    ObservadorEspiao obs;
    auto v2 = base_valida(3);
    std::vector<std::uint8_t> v1(v2.begin(), v2.begin() + kTamCabecalhoSemData);
    v1[4] = static_cast<std::uint8_t>(kVersaoSemData);
    v1[5] = static_cast<std::uint8_t>(kVersaoSemData >> 8);
    v1.insert(v1.end(), v2.begin() + kTamCabecalho, v2.end());
    b.http.corpo_base = v1;
    AtualizadorOta ota(b.cartao, b.rede, b.http, b.pausa, &obs);
    ASSERT_EQ(ota.executa(b.cfg, b.log), ResultadoOta::Atualizada);
    EXPECT_EQ(obs.ultimos_recebidos, obs.ultimo_total);
}

TEST(AtualizadorOta, a_tentativa_aparece_na_fase_de_download) {
    // As tres tentativas do RF05.2 tem de ser visiveis: "BAIXANDO 2/3" diz
    // que algo deu errado e esta sendo refeito, e sem isso uma retentativa
    // parece um travamento.
    Bancada b;
    ObservadorEspiao obs;
    b.cartao.versao_no_cartao = "antiga";
    b.cartao.escrita_falha_em_diante = true;
    b.cartao.cura_na_tentativa = 3;
    AtualizadorOta ota(b.cartao, b.rede, b.http, b.pausa, &obs);
    ota.executa(b.cfg, b.log);

    unsigned maior = 0;
    for (std::size_t i = 0; i < obs.fases.size(); ++i) {
        if (obs.fases[i] == FaseOta::Baixando) {
            maior = obs.tentativas[i] > maior ? obs.tentativas[i] : maior;
        }
    }
    EXPECT_EQ(maior, 3U) << "a contagem de tentativas nao chegou a tela";
}

TEST(AtualizadorOta, sem_observador_nao_quebra) {
    // Toda a suite e o modo de bancada sem painel constroem sem observador.
    Bancada b;
    b.cartao.versao_no_cartao = "antiga";
    b.cartao.resposta_le_versao = ErroCartao::Nenhum;
    AtualizadorOta ota(b.cartao, b.rede, b.http, b.pausa);
    EXPECT_EQ(ota.executa(b.cfg, b.log), ResultadoOta::Atualizada);
}


// --- o motivo curto, para a tela ---
//
// A tela tem uma faixa de 26 caracteres. As frases do LOG sao escritas para
// quem le o arquivo depois, com calma; a da TELA e lida de relance por quem
// esta com o aparelho na mao e precisa decidir o que tentar.

TEST(DescreveCurto, cabe_na_faixa_de_texto_da_tela) {
    // 320 px / 12 px por caractere = 26. Passar disso trunca em silencio, e
    // um motivo truncado e pior do que motivo nenhum.
    for (const auto r : {ResultadoOta::Atualizada, ResultadoOta::JaEstavaEmDia,
                         ResultadoOta::SemConfiguracao, ResultadoOta::FalhaDeRede,
                         ResultadoOta::FalhaAoConsultar, ResultadoOta::FalhaAoBaixar,
                         ResultadoOta::BaseRecusada, ResultadoOta::FalhaAoGravar,
                         ResultadoOta::ServidorRecusou,
                         ResultadoOta::CertificadoRecusado}) {
        EXPECT_LE(std::strlen(descreve_curto(r)), 26u)
            << "motivo longo demais: " << descreve_curto(r);
    }
}

TEST(DescreveCurto, cada_falha_tem_texto_proprio) {
    // O ponto inteiro da mudanca. Se duas falhas dessem a mesma frase, o
    // usuario voltaria a precisar do log para saber qual aconteceu.
    std::set<std::string> vistos;
    for (const auto r : {ResultadoOta::SemConfiguracao, ResultadoOta::FalhaDeRede,
                         ResultadoOta::FalhaAoConsultar, ResultadoOta::FalhaAoBaixar,
                         ResultadoOta::BaseRecusada, ResultadoOta::FalhaAoGravar,
                         ResultadoOta::ServidorRecusou,
                         ResultadoOta::CertificadoRecusado}) {
        EXPECT_TRUE(vistos.insert(descreve_curto(r)).second)
            << "motivo repetido: " << descreve_curto(r);
    }
}

TEST(DescreveCurto, nenhum_resultado_fica_sem_texto) {
    for (const auto r : {ResultadoOta::Atualizada, ResultadoOta::JaEstavaEmDia,
                         ResultadoOta::SemConfiguracao, ResultadoOta::FalhaDeRede,
                         ResultadoOta::FalhaAoConsultar, ResultadoOta::FalhaAoBaixar,
                         ResultadoOta::BaseRecusada, ResultadoOta::FalhaAoGravar,
                         ResultadoOta::ServidorRecusou,
                         ResultadoOta::CertificadoRecusado}) {
        EXPECT_GT(std::strlen(descreve_curto(r)), 0u);
    }
}


TEST(EFalha, os_dois_fins_felizes_nao_sao_falha) {
    EXPECT_FALSE(e_falha(ResultadoOta::Atualizada));
    EXPECT_FALSE(e_falha(ResultadoOta::JaEstavaEmDia));
}

TEST(EFalha, todo_o_resto_e_falha) {
    // "ja estava em dia" e o caso que engana: nada foi baixado, e mesmo
    // assim deu certo. Classifica-lo como falha prenderia a tela esperando
    // o usuario dispensar uma atualizacao que simplesmente nao era precisa.
    for (const auto r : {ResultadoOta::SemConfiguracao, ResultadoOta::FalhaDeRede,
                         ResultadoOta::FalhaAoConsultar, ResultadoOta::FalhaAoBaixar,
                         ResultadoOta::BaseRecusada, ResultadoOta::FalhaAoGravar,
                         ResultadoOta::ServidorRecusou,
                         ResultadoOta::CertificadoRecusado}) {
        EXPECT_TRUE(e_falha(r)) << descreve(r);
    }
}


// ================================ o segredo NAO vai na URL

TEST(OtaComToken, o_token_nao_entra_na_url) {
    // Ele vai no cabeçalho `X-Coruja-Token`, que o `ClienteTls` acrescenta.
    // Houve uma versão em que vinha como `?t=` -- limitação do `http_client`
    // do lwIP, que montava o pedido sem gancho para cabeçalho próprio. Com
    // TLS o transporte foi reescrito e o segredo saiu da URL, onde entrava no
    // log de acesso do servidor e de qualquer proxy.
    Bancada b;
    std::snprintf(b.cfg.token_aparelho, sizeof b.cfg.token_aparelho,
                  "segredo-do-carro");
    ASSERT_EQ(b.roda(), ResultadoOta::Atualizada);

    ASSERT_GE(b.http.caminhos.size(), 2U);
    for (const auto& c : b.http.caminhos) {
        EXPECT_EQ(c.find("segredo-do-carro"), std::string::npos) << c;
        EXPECT_EQ(c.find("?t="), std::string::npos) << c;
    }
}

TEST(OtaComToken, o_token_nunca_aparece_no_log) {
    // Mesma regra da senha de Wi-Fi: o log vai para o cartão e o cartão sai
    // do carro.
    Bancada b;
    std::snprintf(b.cfg.token_aparelho, sizeof b.cfg.token_aparelho,
                  "nao-pode-vazar");
    b.roda();
    for (const auto& e : b.log.entradas()) {
        EXPECT_EQ(e.mensagem.find("nao-pode-vazar"), std::string::npos)
            << e.mensagem;
    }
}

// ================================================= o cao de guarda (19/11)

TEST(AtualizadorOta, cada_pedaco_gravado_alimenta_o_cao) {
    // **A regra: byte que chegou E foi para o cartao.** E o progresso de
    // verdade do download -- nao o tempo decorrido, nao a volta do laco de
    // espera. O duble entrega em pedacos de 64 B, como a rede faz.
    Bancada b;
    const std::size_t pedacos = (b.http.corpo_base.size() + 63) / 64;
    ASSERT_EQ(b.roda(), ResultadoOta::Atualizada);
    EXPECT_GE(b.http.refeicoes_no_pedido, pedacos)
        << "o download andou sem alimentar o cao";
}

TEST(AtualizadorOta, o_download_que_nao_entrega_nada_nao_alimenta_o_cao) {
    // **Este e o teste que torna o cao util.** Um pedido que gasta o tempo
    // inteiro sem um byte chegar NAO pode produzir refeicao nenhuma: se
    // produzisse, um mbedTLS girando em falso manteria o aparelho mudo e
    // vigiado para sempre, que e o estado de hoje com um custo a mais.
    //
    // As refeicoes das pontas -- antes de pedir, depois de voltar -- nao
    // entram: o duble so conta o que aconteceu DENTRO do pedido.
    Bancada b;
    b.http.corpo_base.clear();
    b.http.resposta_base = ResultadoHttp{ErroHttp::TempoEsgotado, 0, 0};
    b.http.cura_na_tentativa = 99;   // nunca sara: o pedido sempre estoura
    b.http.gasta_ms = 15000;   // a janela de inatividade do ClienteTls
    ASSERT_EQ(b.roda(), ResultadoOta::FalhaAoBaixar);
    EXPECT_EQ(b.http.refeicoes_no_pedido, 0U)
        << "o cao foi alimentado por um download que nao progrediu";
}

TEST(AtualizadorOta, tres_tentativas_lentas_nao_deixam_o_cao_sem_comer) {
    // O pior caminho de tempo do OTA: tres downloads que gastam a janela de
    // inatividade inteira, com 5 s de espera entre eles. Nenhum trecho pode
    // passar do prazo.
    //
    // Sem a refeicao no RETORNO de cada pedido, os 15 s do download e os 5 s
    // da espera seguinte viram um bloco de 20 s -- acima do prazo, e o
    // aparelho reiniciaria sozinho toda vez que o Wi-Fi ficasse ruim.
    Bancada b;
    b.http.corpo_base.clear();
    b.http.resposta_base = ResultadoHttp{ErroHttp::TempoEsgotado, 0, 0};
    b.http.cura_na_tentativa = 99;   // nunca sara: o pedido sempre estoura
    b.http.gasta_ms = 15000;
    ASSERT_EQ(b.roda(), ResultadoOta::FalhaAoBaixar);
    ASSERT_EQ(b.http.pedidos_base, kTentativas);
    EXPECT_LE(b.cao.maior_intervalo_ms, kPrazoCaoMs)
        << "houve um trecho maior que o prazo: o cao morderia";
}

TEST(AtualizadorOta, a_associacao_do_radio_roda_com_o_cao_solto) {
    // A varredura (12 s) e a associacao (20 s) do `RedeWifi` sao chamadas do
    // SDK sem gancho por dentro, e a segunda passa do teto de 16,777 s do
    // hardware. Manter o cao armado ali seria reiniciar o aparelho em toda
    // conexao que falha -- senha errada, rede fora de alcance --, que e
    // desfecho comum e nao travamento.
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoOta::Atualizada);
    EXPECT_EQ(b.rede.suspensoes_ao_conectar, 1U)
        << "o cao estava armado durante a associacao";
    EXPECT_EQ(b.rede.retomadas_ao_conectar, 0U);
    EXPECT_EQ(b.cao.retomadas, 1U) << "o cao ficou solto depois de conectar";
}

TEST(AtualizadorOta, a_leitura_da_versao_local_fecha_o_trecho_antes_de_soltar_o_cao) {
    // `le_versao_local` e cartao puro, sem gancho, e vem depois do que
    // `AcoesDoAparelho` ja leu do cartao (log, configuracao) sem refeicao
    // entre a ultima volta do laco e a associacao. Sem uma refeicao aqui os
    // trechos de cartao se somariam ate o `suspende`.
    Bancada b;
    ASSERT_EQ(b.roda(), ResultadoOta::Atualizada);
    EXPECT_GE(b.rede.alimentacoes_ao_conectar, 1U)
        << "nenhuma refeicao entre a leitura da versao e a associacao";
}

TEST(AtualizadorOta, cartao_degradado_nao_deixa_o_cao_sem_comer_do_retoma_ao_fim) {
    // **Cada etapa de cartao ou de rede custa 9 s**, o que um cartao
    // degradado faz (2 s de timeout por comando, tres tentativas) e o SNTP
    // quase faz (5 s + 5 s). Dois trechos adjacentes passam de 16,5 s:
    // so ha prazo se CADA etapa fecha o seu trecho com uma refeicao.
    //
    //  retoma -> preparo -> [388] -> consulta -> [391] -> abre -> [501]
    //  -> baixa (por pedaco) -> conclui -> [566] -> promove -> [580]
    //  -> grava a versao -> fim
    Bancada b;
    b.preparo.custo_ms = 9000;
    b.http.gasta_ms = 9000;
    b.cartao.custo_abre_ms = 9000;
    b.cartao.custo_conclui_ms = 9000;
    b.cartao.custo_promove_ms = 9000;
    b.cartao.custo_grava_ms = 9000;
    ASSERT_EQ(b.roda(), ResultadoOta::Atualizada);
    EXPECT_LE(b.cao.maior_intervalo_ms, 9000u)
        << "um trecho ficou " << b.cao.maior_intervalo_ms
        << " ms sem refeicao";
}

TEST(AtualizadorOta, a_espera_entre_tentativas_nao_soma_com_a_abertura_do_cartao) {
    // Um download que falha, 5 s de espera e a abertura lenta do arquivo
    // temporario: sem a refeicao DEPOIS da espera, 5 + 12 passam de 16,5 s.
    Bancada b;
    b.http.resposta_base = ResultadoHttp{ErroHttp::TempoEsgotado, 0, 0};
    b.http.cura_na_tentativa = 2;
    b.cartao.custo_abre_ms = 12000;
    ASSERT_EQ(b.roda(), ResultadoOta::Atualizada);
    ASSERT_EQ(b.http.pedidos_base, 2U);
    EXPECT_LE(b.cao.maior_intervalo_ms, 12000u)
        << "um trecho ficou " << b.cao.maior_intervalo_ms
        << " ms sem refeicao";
}

TEST(AtualizadorOta, a_rede_que_falha_tambem_devolve_o_cao) {
    // Sair pelo caminho de erro nao pode deixar o aparelho sem vigilancia
    // pelo resto da ligacao.
    Bancada b;
    b.rede.resposta = ErroWifi::NenhumaRedeVisivel;
    ASSERT_EQ(b.roda(), ResultadoOta::FalhaDeRede);
    EXPECT_EQ(b.cao.suspensoes, 1U);
    EXPECT_EQ(b.cao.retomadas, 1U);
}

TEST(AtualizadorOta, sem_cao_a_atualizacao_roda_igual) {
    Bancada b;
    AtualizadorOta ota(b.cartao, b.rede, b.http, b.pausa);
    EXPECT_EQ(ota.executa(b.cfg, b.log), ResultadoOta::Atualizada);
}

}  // namespace
