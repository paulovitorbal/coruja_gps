#include "rede/AtualizadorOta.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

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
        return resposta_conclui;
    }

    void descarta_escrita(const char* nome, Logger&) override {
        ++descartes;
        chamadas.push_back(std::string("descarta:") + nome);
    }

    ErroCartao promove(const char* tmp, const char* base, const char* reserva,
                       Logger&) override {
        chamadas.emplace_back("promove");
        promoveu_de = tmp; promoveu_para = base; promoveu_reserva = reserva;
        return resposta_promove;
    }
};

class ConexaoFalsa : public Conexao {
public:
    ErroWifi resposta = ErroWifi::Nenhum;
    unsigned conexoes = 0;
    unsigned desconexoes = 0;
    ErroWifi conecta(const Configuracao&, Logger&) override {
        ++conexoes;
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
    unsigned          pedidos_base = 0;

    ResultadoHttp baixa(const Url& url, AoReceber ao_receber, void* contexto,
                        Logger&, std::uint32_t) override {
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
            ao_receber(contexto, corpo_base.data() + i, n);
        }
        if (pedidos_base < cura_na_tentativa) { return resposta_base; }
        return ResultadoHttp{};
    }
};

class PausaFalsa : public Pausa {
public:
    std::vector<std::uint32_t> esperas;
    void espera_ms(std::uint32_t ms) override { esperas.push_back(ms); }
};

// ================================================================== cenário

struct Bancada {
    ArmazenamentoFalso cartao;
    ConexaoFalsa       rede;
    BaixadorFalso      http;
    PausaFalsa         pausa;
    teste::LoggerMock  log;
    Configuracao       cfg;

    Bancada() {
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
        AtualizadorOta ota(cartao, rede, http, pausa);
        return ota.executa(cfg, log);
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

TEST(AtualizadorOta, versao_igual_nao_baixa_a_base) {
    Bancada b;
    b.cartao.resposta_le_versao = ErroCartao::Nenhum;
    b.cartao.versao_no_cartao = "2026-09-28";
    b.http.corpo_versao = "2026-09-28";
    EXPECT_EQ(b.roda(), ResultadoOta::JaEstavaEmDia);
    EXPECT_EQ(b.http.pedidos_base, 0U) << "baixou 214 KB a toa";
    EXPECT_EQ(b.rede.desconexoes, 1U);
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

}  // namespace
