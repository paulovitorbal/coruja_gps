#include "rede/RemessaDados.h"

#include "armazenamento/CartaoSd.h"
#include "apoio/fatfs/FatFsFalso.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "apoio/LoggerMock.h"
#include "nucleo/Crc32.h"

namespace {

using namespace coruja;

std::uint32_t crc_de(const std::string& s) {
    return crc32(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

// =========================================================== dublê de cartão

/// Um cartão em memória. Guarda o conteúdo, serve a leitura em pedaços e
/// registra o que foi apagado — que é o desfecho que mais importa aqui.
class CartaoFalso : public Arquivario {
public:
    std::map<std::string, std::string> arquivos;
    std::vector<std::string>           apagados;
    bool falha_ao_listar = false;
    bool falha_ao_abrir = false;
    bool falha_ao_rebobinar = false;
    bool falha_no_meio_da_leitura = false;
    bool falha_ao_remover = false;
    /// Quantos bytes ler antes de a leitura falhar, quando configurada.
    std::size_t bytes_ate_falhar = 0;
    /// A falha só é armada DEPOIS do `rebobina()`, ou seja, na segunda
    /// passada -- o cartão lê inteiro para o CRC e falha durante o envio.
    /// É o que acontece quando alguém puxa o cartão no meio da remessa.
    bool falha_so_na_segunda_passada = false;
    /// Acrescentado ao arquivo assim que a primeira leitura termina — simula
    /// o log crescendo durante a remessa.
    std::string cresce_depois_de_ler;
    std::string cresce_em;

    ErroCartao lista(AoListar ao_listar, void* ctx, Logger&) override {
        if (falha_ao_listar) { return ErroCartao::SemCartaoLegivel; }
        for (const auto& [nome, dado] : arquivos) {
            ao_listar(ctx, nome.c_str(), dado.size());
        }
        return ErroCartao::Nenhum;
    }

    ErroCartao abre_para_leitura(const char* nome, std::size_t* tamanho,
                                 Logger&) override {
        if (falha_ao_abrir) { return ErroCartao::FalhaDeLeitura; }
        auto it = arquivos.find(nome);
        if (it == arquivos.end()) { return ErroCartao::ArquivoAusente; }
        aberto_ = nome;
        pos_ = 0;
        lidos_nesta_passada_ = 0;
        rebobinou_ = false;
        *tamanho = it->second.size();
        return ErroCartao::Nenhum;
    }

    bool le(std::uint8_t* destino, std::size_t capacidade,
            std::size_t* lidos) override {
        if (aberto_.empty()) { return false; }
        const bool armada = falha_no_meio_da_leitura
                            && (!falha_so_na_segunda_passada || rebobinou_);
        if (armada && lidos_nesta_passada_ >= bytes_ate_falhar) {
            return false;
        }
        const std::string& dado = arquivos[aberto_];
        const std::size_t resta = dado.size() > pos_ ? dado.size() - pos_ : 0;
        const std::size_t n = std::min(capacidade, resta);
        std::memcpy(destino, dado.data() + pos_, n);
        pos_ += n;
        lidos_nesta_passada_ += n;
        *lidos = n;
        if (n == 0) { return false; }
        return true;
    }

    bool rebobina() override {
        if (falha_ao_rebobinar) { return false; }
        if (!cresce_em.empty() && cresce_em == aberto_) {
            arquivos[cresce_em] += cresce_depois_de_ler;
        }
        pos_ = 0;
        lidos_nesta_passada_ = 0;
        rebobinou_ = true;
        return true;
    }

    void fecha_leitura() override { aberto_.clear(); }

    ErroCartao remove(const char* nome, Logger&) override {
        if (falha_ao_remover) { return ErroCartao::FalhaDeEscrita; }
        if (arquivos.erase(nome) == 0) { return ErroCartao::ArquivoAusente; }
        apagados.push_back(nome);
        return ErroCartao::Nenhum;
    }

private:
    std::string aberto_;
    std::size_t pos_ = 0;
    std::size_t lidos_nesta_passada_ = 0;
    bool        rebobinou_ = false;
};

// ============================================================ dublê de rede

class RedeFalsa : public Conexao {
public:
    ErroWifi erro = ErroWifi::Nenhum;
    int conectou = 0;
    int desconectou = 0;
    std::vector<std::string>* diario = nullptr;

    ErroWifi conecta(const Configuracao&, Logger&) override {
        ++conectou;
        if (diario != nullptr) { diario->push_back("conecta"); }
        return erro;
    }
    void desconecta(Logger&) override { ++desconectou; }
};

// ========================================================= dublê de servidor

/// Guarda o que recebeu e responde o CRC do que guardou — como o servidor de
/// referência faz. Por padrão é um servidor que funciona.
class ServidorFalso : public Enviador {
public:
    std::map<std::string, std::string> recebidos;
    std::vector<std::string>           caminhos;
    std::vector<std::string>           tokens;
    ErroHttp erro_no_envio = ErroHttp::Nenhum;
    ErroHttp erro_na_consulta = ErroHttp::Nenhum;
    /// Corrompe um byte do que chegar: o servidor guarda algo diferente.
    bool corrompe = false;
    /// Responde o CRC de outro conteúdo, sem corromper o guardado.
    bool mente_no_crc = false;

    ResultadoHttp envia(const Url& url, const char* token, FonteDeBytes fonte,
                        void* ctx, std::size_t tamanho, Logger&,
                        std::uint32_t) override {
        caminhos.push_back(url.caminho);
        tokens.push_back(token == nullptr ? "" : token);
        if (erro_no_envio != ErroHttp::Nenhum) {
            return {erro_no_envio, 0, 0, };
        }
        std::string corpo;
        std::uint8_t buf[256];
        while (corpo.size() < tamanho) {
            const std::size_t n = fonte(ctx, buf, sizeof buf);
            if (n == 0) { break; }
            corpo.append(reinterpret_cast<char*>(buf), n);
        }
        // Como o servidor de referência: corpo menor que o Content-Length é
        // recusado e NADA é guardado.
        if (corpo.size() != tamanho) {
            return {ErroHttp::StatusNaoOk, 400, corpo.size()};
        }
        if (corrompe && !corpo.empty()) { corpo[0] = static_cast<char>(corpo[0] ^ 0xFF); }
        recebidos[nome_de(url)] = corpo;
        return {ErroHttp::Nenhum, 201, corpo.size()};
    }

    ResultadoHttp consulta_crc(const Url& url, const char* token,
                               std::uint32_t* crc, Logger&,
                               std::uint32_t) override {
        tokens.push_back(token == nullptr ? "" : token);
        auto it = recebidos.find(nome_de(url));
        if (erro_na_consulta != ErroHttp::Nenhum) {
            // Parametro de saida apos chamada que falhou contem LIXO, e o
            // dublê devolve o pior lixo possivel: o valor certo. Um
            // orquestrador que olhasse so o CRC e ignorasse o `ok()`
            // apagaria o arquivo por causa de um acaso.
            if (it != recebidos.end()) { *crc = crc_de(it->second); }
            return {erro_na_consulta, 0, 0};
        }
        if (it == recebidos.end()) { return {ErroHttp::StatusNaoOk, 404, 0}; }
        *crc = mente_no_crc ? crc_de(it->second + "x") : crc_de(it->second);
        return {ErroHttp::Nenhum, 200, 8};
    }

    static std::string nome_de(const Url& url) {
        const std::string c = url.caminho;
        const auto barra = c.rfind('/');
        return barra == std::string::npos ? c : c.substr(barra + 1);
    }
};

// =================================================================== apoio

Configuracao cfg_valida() {
    Configuracao cfg;
    cfg.n_redes = 1;
    std::snprintf(cfg.redes[0].ssid, sizeof cfg.redes[0].ssid, "casa");
    std::snprintf(cfg.redes[0].senha, sizeof cfg.redes[0].senha, "12345678");
    std::snprintf(cfg.url_envio, sizeof cfg.url_envio,
                  "http://servidor.exemplo/envio/");
    std::snprintf(cfg.token_aparelho, sizeof cfg.token_aparelho, "segredo");
    return cfg;
}

/// Anota a ORDEM dos acontecimentos, que e o que importa aqui.
class PreparoEspiao : public PreparoDeSessao {
public:
    std::vector<std::string>* diario = nullptr;
    void apos_conectar(Logger&) override {
        if (diario != nullptr) { diario->push_back("preparo"); }
    }
};

struct Cenario {
    CartaoFalso   cartao;
    RedeFalsa     rede;
    ServidorFalso servidor;
    teste::LoggerMock log;
    Configuracao  cfg = cfg_valida();
    PreparoEspiao preparo;
    std::vector<std::string> diario;

    ResultadoRemessa roda() {
        rede.diario = &diario;
        preparo.diario = &diario;
        RemessaDados r(cartao, rede, servidor, nullptr, &preparo);
        auto saida = r.executa(cfg, log);
        entregues = r.entregues();
        falhados = r.falhados();
        retidos = r.retidos();
        return saida;
    }
    unsigned entregues = 0;
    unsigned falhados = 0;
    unsigned retidos = 0;
};

// ====================== a ordem: conectar ANTES de preparar a sessao

TEST(Remessa, o_preparo_da_sessao_vem_DEPOIS_de_conectar) {
    // Travou o aparelho inteiro em 2026-10-06.
    //
    // O relogio era acertado na composicao, ANTES de o orquestrador rodar.
    // Mas quem chama `cyw43_arch_init()` e o `RedeWifi::conecta()`, la
    // dentro -- entao o cliente NTP falava com a pilha de rede e com o radio
    // antes de os dois existirem. Tela congelada, encoder morto, nem uma
    // linha de log.
    Cenario c;
    c.cartao.arquivos["coruja.log"] = "dado";
    ASSERT_EQ(c.roda(), ResultadoRemessa::Enviada);
    EXPECT_EQ(c.diario, (std::vector<std::string>{"conecta", "preparo"}));
}

TEST(Remessa, sem_rede_o_preparo_NAO_acontece) {
    // Preparar sem rede e exatamente o que travava: falar com a pilha antes
    // de ela existir.
    Cenario c;
    c.rede.erro = ErroWifi::FalhaDeAssociacao;
    c.cartao.arquivos["coruja.log"] = "dado";
    EXPECT_EQ(c.roda(), ResultadoRemessa::FalhaDeRede);
    EXPECT_EQ(c.diario, (std::vector<std::string>{"conecta"}));
}

TEST(Remessa, o_preparo_acontece_UMA_vez_por_sessao) {
    Cenario c;
    c.cartao.arquivos["coruja.log"] = "a";
    c.cartao.arquivos["infracoes.log"] = "b";
    ASSERT_EQ(c.roda(), ResultadoRemessa::Enviada);
    EXPECT_EQ(std::count(c.diario.begin(), c.diario.end(), "preparo"), 1);
}

// ======================================================= quais nomes sobem

TEST(NomeEnviavel, aceita_os_tres_que_o_firmware_gera) {
    EXPECT_TRUE(nome_enviavel("coruja.log"));
    EXPECT_TRUE(nome_enviavel("infracoes.log"));
    EXPECT_TRUE(nome_enviavel("20261006_143000.log"));
}

TEST(NomeEnviavel, recusa_o_que_nao_pode_sair_do_cartao) {
    // `coruja.cfg` tem a senha do Wi-Fi e `radares.bin` tem 214 KB que o
    // servidor acabou de mandar para cá.
    EXPECT_FALSE(nome_enviavel("coruja.cfg"));
    EXPECT_FALSE(nome_enviavel("radares.bin"));
    EXPECT_FALSE(nome_enviavel("versao.txt"));
    EXPECT_FALSE(nome_enviavel("radares.bak"));
}

TEST(NomeEnviavel, exige_a_forma_inteira_do_nome_de_viagem) {
    // Conferir só a extensão deixaria passar qualquer `.log` que outra
    // ferramenta tenha deixado no cartão -- e ele seria apagado.
    EXPECT_FALSE(nome_enviavel("qualquer.log"));
    EXPECT_FALSE(nome_enviavel("2026100_143000.log"));   // 7 dígitos
    EXPECT_FALSE(nome_enviavel("20261006_14300.log"));   // 5 dígitos
    EXPECT_FALSE(nome_enviavel("20261006-143000.log"));  // separador errado
    EXPECT_FALSE(nome_enviavel("2026100a_143000.log"));  // letra no meio
    EXPECT_FALSE(nome_enviavel("20261006_143000.txt"));
    EXPECT_FALSE(nome_enviavel("20261006_143000.log.bak"));
    EXPECT_FALSE(nome_enviavel(""));
    EXPECT_FALSE(nome_enviavel(nullptr));
}

// ================================================================ pré-voo

TEST(Remessa, sem_url_nao_tenta_nada) {
    Cenario c;
    c.cfg.url_envio[0] = '\0';
    c.cartao.arquivos["coruja.log"] = "qualquer coisa";
    EXPECT_EQ(c.roda(), ResultadoRemessa::SemConfiguracao);
    EXPECT_EQ(c.rede.conectou, 0);
    EXPECT_TRUE(c.cartao.apagados.empty());
}

TEST(Remessa, sem_token_nao_tenta_nada) {
    // URL sozinha significaria mandar para um servidor que aceita de
    // qualquer um. Exigir os dois impede configurar isso por descuido.
    Cenario c;
    c.cfg.token_aparelho[0] = '\0';
    c.cartao.arquivos["coruja.log"] = "qualquer coisa";
    EXPECT_EQ(c.roda(), ResultadoRemessa::SemConfiguracao);
    EXPECT_EQ(c.rede.conectou, 0);
}

TEST(Remessa, sem_rede_configurada_nao_tenta_nada) {
    Cenario c;
    c.cfg.n_redes = 0;
    EXPECT_EQ(c.roda(), ResultadoRemessa::SemConfiguracao);
}

TEST(Remessa, falha_de_wifi_desconecta_do_mesmo_jeito) {
    Cenario c;
    c.rede.erro = ErroWifi::FalhaDeAssociacao;
    c.cartao.arquivos["coruja.log"] = "dado";
    EXPECT_EQ(c.roda(), ResultadoRemessa::FalhaDeRede);
    EXPECT_TRUE(c.servidor.recebidos.empty());
}

TEST(Remessa, cartao_vazio_e_sucesso_sem_trabalho) {
    Cenario c;
    EXPECT_EQ(c.roda(), ResultadoRemessa::NadaAEnviar);
    EXPECT_EQ(c.rede.desconectou, 1);
}

// ============================================================ caminho feliz

TEST(Remessa, envia_confirma_e_apaga) {
    Cenario c;
    c.cartao.arquivos["infracoes.log"] = "passagem em 60 km/h";
    ASSERT_EQ(c.roda(), ResultadoRemessa::Enviada);

    EXPECT_EQ(c.servidor.recebidos["infracoes.log"], "passagem em 60 km/h");
    EXPECT_EQ(c.cartao.apagados, std::vector<std::string>{"infracoes.log"});
    EXPECT_TRUE(c.cartao.arquivos.empty());
    EXPECT_EQ(c.entregues, 1u);
}

TEST(Remessa, manda_os_tres_tipos_de_arquivo) {
    Cenario c;
    c.cartao.arquivos["coruja.log"] = "diario";
    c.cartao.arquivos["infracoes.log"] = "multas";
    c.cartao.arquivos["20261006_143000.log"] = "viagem";
    ASSERT_EQ(c.roda(), ResultadoRemessa::Enviada);
    EXPECT_EQ(c.servidor.recebidos.size(), 3u);
    EXPECT_EQ(c.entregues, 3u);
}

TEST(Remessa, nao_manda_nem_apaga_o_que_nao_e_dele) {
    Cenario c;
    c.cartao.arquivos["coruja.cfg"] = "wifi_senha_1=segredo";
    c.cartao.arquivos["radares.bin"] = "base inteira";
    c.cartao.arquivos["versao.txt"] = "crc32:aa";
    ASSERT_EQ(c.roda(), ResultadoRemessa::NadaAEnviar);
    EXPECT_TRUE(c.servidor.recebidos.empty());
    EXPECT_TRUE(c.cartao.apagados.empty());
    EXPECT_EQ(c.cartao.arquivos.size(), 3u);
}

TEST(Remessa, o_arquivo_chega_byte_a_byte_mesmo_passando_de_um_pedaco) {
    // Maior que o pedaço de 1 KB: o laço tem de dar mais de uma volta, que é
    // onde erro de contagem aparece.
    Cenario c;
    std::string grande;
    for (int i = 0; i < 400; ++i) { grande += "linha de registro numero X\n"; }
    ASSERT_GT(grande.size(), kPedacoRemessa * 2);
    c.cartao.arquivos["coruja.log"] = grande;
    ASSERT_EQ(c.roda(), ResultadoRemessa::Enviada);
    EXPECT_EQ(c.servidor.recebidos["coruja.log"], grande);
}

TEST(Remessa, o_token_acompanha_envio_e_consulta) {
    Cenario c;
    c.cartao.arquivos["coruja.log"] = "dado";
    ASSERT_EQ(c.roda(), ResultadoRemessa::Enviada);
    ASSERT_GE(c.servidor.tokens.size(), 2u);
    for (const auto& t : c.servidor.tokens) { EXPECT_EQ(t, "segredo"); }
}

TEST(Remessa, a_url_sai_com_o_nome_do_arquivo_no_fim) {
    Cenario c;
    c.cartao.arquivos["20261006_143000.log"] = "viagem";
    ASSERT_EQ(c.roda(), ResultadoRemessa::Enviada);
    ASSERT_EQ(c.servidor.caminhos.size(), 1u);
    EXPECT_EQ(c.servidor.caminhos[0], "/envio/20261006_143000.log");
}

TEST(Remessa, url_sem_barra_no_fim_nao_cola_o_nome_no_caminho) {
    // O erro mais provável de quem edita o cfg à mão. Sem normalizar, isto
    // viraria "/enviocoruja.log" e o servidor responderia 404 para sempre.
    Cenario c;
    std::snprintf(c.cfg.url_envio, sizeof c.cfg.url_envio,
                  "http://servidor.exemplo/envio");
    c.cartao.arquivos["coruja.log"] = "dado";
    ASSERT_EQ(c.roda(), ResultadoRemessa::Enviada);
    EXPECT_EQ(c.servidor.caminhos[0], "/envio/coruja.log");
}

TEST(Remessa, url_invalida_nao_apaga_nada) {
    Cenario c;
    std::snprintf(c.cfg.url_envio, sizeof c.cfg.url_envio, "nao-e-url");
    c.cartao.arquivos["coruja.log"] = "dado";
    EXPECT_EQ(c.roda(), ResultadoRemessa::FalhaAoEnviar);
    EXPECT_TRUE(c.cartao.apagados.empty());
}

// ================================================= o CRC é que autoriza apagar

TEST(Remessa, conteudo_que_chegou_diferente_nao_libera_o_apagamento) {
    // O ponto inteiro do recurso. O servidor guardou algo, respondeu, e o que
    // ele guardou não é o que saiu daqui -- apagar aqui perderia o único
    // exemplar.
    Cenario c;
    c.servidor.corrompe = true;
    c.cartao.arquivos["infracoes.log"] = "registro que nao pode sumir";
    EXPECT_EQ(c.roda(), ResultadoRemessa::FalhaAoEnviar);
    EXPECT_TRUE(c.cartao.apagados.empty());
    EXPECT_EQ(c.cartao.arquivos["infracoes.log"],
              "registro que nao pode sumir");
}

TEST(Remessa, crc_que_nao_bate_nao_libera_o_apagamento) {
    Cenario c;
    c.servidor.mente_no_crc = true;
    c.cartao.arquivos["coruja.log"] = "dado";
    EXPECT_EQ(c.roda(), ResultadoRemessa::FalhaAoEnviar);
    EXPECT_TRUE(c.cartao.apagados.empty());
}

TEST(Remessa, sem_confirmacao_nao_apaga) {
    // O envio foi bem, o arquivo ESTÁ no servidor, e a consulta falhou. O
    // dublê ainda escreve o CRC certo no parâmetro de saída -- como uma
    // chamada real poderia deixar lixo que por acaso coincide. Quem decidisse
    // só pelo CRC apagaria aqui.
    Cenario c;
    c.servidor.erro_na_consulta = ErroHttp::TempoEsgotado;
    c.cartao.arquivos["coruja.log"] = "dado";
    EXPECT_EQ(c.roda(), ResultadoRemessa::FalhaAoEnviar);
    EXPECT_TRUE(c.cartao.apagados.empty());
    EXPECT_EQ(c.cartao.arquivos.count("coruja.log"), 1u);
    EXPECT_TRUE(c.log.contem(Nivel::Warning, "nao confirmou"));
}

TEST(Remessa, envio_que_falhou_nao_apaga_e_diz_que_foi_a_rede) {
    // A guarda do `envio.ok()` é redundante com a do CRC para o APAGAMENTO --
    // o servidor não teria o arquivo e a confirmação falharia de qualquer
    // jeito. Ela existe pela mensagem: quem lê o log precisa saber que a
    // conexão caiu, e não que "o servidor não confirmou", que mandaria
    // procurar defeito no lugar errado.
    Cenario c;
    c.servidor.erro_no_envio = ErroHttp::Interrompida;
    c.cartao.arquivos["coruja.log"] = "dado";
    EXPECT_EQ(c.roda(), ResultadoRemessa::FalhaAoEnviar);
    EXPECT_TRUE(c.cartao.apagados.empty());
    EXPECT_TRUE(c.log.contem(Nivel::Error, "nao subiu"));
    EXPECT_FALSE(c.log.contem(Nivel::Warning, "nao confirmou"));
}

TEST(Remessa, leitura_que_falha_no_meio_do_envio_acusa_o_cartao_e_nao_a_rede) {
    // Mesma razão do teste acima, do outro lado: sem a marca da bomba, o
    // sintoma seria "o servidor recusou" -- e o defeito está no cartão. São
    // consertos diferentes, e o log é a única pista de qual tentar.
    Cenario c;
    c.cartao.arquivos["coruja.log"] = std::string(3000, 'x');
    c.cartao.falha_no_meio_da_leitura = true;
    c.cartao.falha_so_na_segunda_passada = true;
    c.cartao.bytes_ate_falhar = 1024;
    EXPECT_EQ(c.roda(), ResultadoRemessa::FalhaAoEnviar);
    EXPECT_TRUE(c.cartao.apagados.empty());
    EXPECT_TRUE(c.log.contem(Nivel::Error, "falhou no meio do envio"));
    EXPECT_FALSE(c.log.contem(Nivel::Error, "nao subiu"));
}

TEST(Remessa, cartao_que_falha_na_soma_do_crc_nem_tenta_enviar) {
    // Falha antes do primeiro byte sair: mandar um arquivo que nem se
    // conseguiu ler inteiro gastaria rede para produzir um CRC que nunca
    // bateria.
    Cenario c;
    c.cartao.arquivos["coruja.log"] = std::string(3000, 'x');
    c.cartao.falha_no_meio_da_leitura = true;
    c.cartao.bytes_ate_falhar = 1024;
    EXPECT_EQ(c.roda(), ResultadoRemessa::FalhaAoEnviar);
    EXPECT_TRUE(c.servidor.recebidos.empty());
    EXPECT_TRUE(c.cartao.apagados.empty());
    EXPECT_TRUE(c.log.contem(Nivel::Error, "ilegivel"));
}

// ====================================== o arquivo que cresce durante o envio

TEST(Remessa, arquivo_que_cresceu_fica_no_cartao_mas_conta_como_entregue) {
    // O `coruja.log` e o registro da viagem em curso crescem ENQUANTO a
    // remessa roda. Apagar depois da confirmação de N bytes destruiria os
    // bytes escritos depois -- e sem deixar rastro.
    Cenario c;
    c.cartao.arquivos["20261006_143000.log"] = "trecho ja gravado";
    c.cartao.cresce_em = "20261006_143000.log";
    c.cartao.cresce_depois_de_ler = "\nmais um ponto depois";

    ASSERT_EQ(c.roda(), ResultadoRemessa::Enviada);
    EXPECT_EQ(c.entregues, 1u);
    EXPECT_EQ(c.retidos, 1u);
    EXPECT_TRUE(c.cartao.apagados.empty());
    // O que cresceu continua lá, inteiro, para a próxima remessa.
    EXPECT_EQ(c.cartao.arquivos["20261006_143000.log"],
              "trecho ja gravado\nmais um ponto depois");
}

TEST(Remessa, falha_ao_apagar_nao_derruba_a_remessa) {
    // O conteúdo está a salvo no servidor; não conseguir apagar é sujeira,
    // não perda.
    Cenario c;
    c.cartao.falha_ao_remover = true;
    c.cartao.arquivos["coruja.log"] = "dado";
    EXPECT_EQ(c.roda(), ResultadoRemessa::Enviada);
    EXPECT_EQ(c.entregues, 1u);
    EXPECT_EQ(c.retidos, 1u);
}

// ================================================= falhas de cartão e limites

TEST(Remessa, cartao_que_nao_lista_nao_e_cartao_vazio) {
    // "Não consegui ler" e "não havia nada" levam a conclusões opostas para
    // quem lê o log, e a mesma resposta esconderia um cartão com defeito.
    Cenario c;
    c.cartao.falha_ao_listar = true;
    EXPECT_EQ(c.roda(), ResultadoRemessa::FalhaDeCartao);
    EXPECT_EQ(c.rede.desconectou, 1);
}

TEST(Remessa, arquivo_vazio_e_pulado_sem_ser_apagado) {
    // Corpo de zero byte o servidor recusa com 400. Pular evita insistir a
    // cada remessa; não apagar mantém a regra sem exceção.
    Cenario c;
    c.cartao.arquivos["coruja.log"] = "";
    EXPECT_EQ(c.roda(), ResultadoRemessa::NadaAEnviar);
    EXPECT_TRUE(c.cartao.apagados.empty());
    EXPECT_EQ(c.cartao.arquivos.count("coruja.log"), 1u);
}

TEST(Remessa, alem_do_teto_fica_para_a_proxima_e_nao_se_perde) {
    Cenario c;
    for (std::size_t i = 0; i < kMaxRemessa + 3; ++i) {
        char nome[32];
        std::snprintf(nome, sizeof nome, "202610%02u_120000.log",
                      static_cast<unsigned>(i + 1));
        c.cartao.arquivos[nome] = "viagem";
    }
    // Parcial e não Enviada: sobrou coisa, e dizer "tudo entregue" seria
    // mentir para quem lê a tela.
    EXPECT_EQ(c.roda(), ResultadoRemessa::Parcial);
    EXPECT_EQ(c.entregues, kMaxRemessa);
    EXPECT_EQ(c.cartao.arquivos.size(), 3u);

    // A segunda remessa leva o resto.
    Cenario& c2 = c;
    c2.servidor.recebidos.clear();
    EXPECT_EQ(c2.roda(), ResultadoRemessa::Enviada);
    EXPECT_TRUE(c2.cartao.arquivos.empty());
}

TEST(Remessa, arquivo_vazio_no_meio_nao_atrapalha_os_outros) {
    Cenario c;
    c.cartao.arquivos["coruja.log"] = "diario";
    c.cartao.arquivos["infracoes.log"] = "";  // vazio: nem entra na lista
    c.cartao.arquivos["20261006_143000.log"] = "viagem";
    c.cartao.arquivos["20261007_143000.log"] = "outra viagem";
    ASSERT_EQ(c.roda(), ResultadoRemessa::Enviada);
    EXPECT_EQ(c.entregues, 3u);
}

TEST(Remessa, falha_num_arquivo_nao_impede_os_seguintes) {
    Cenario c;
    c.cartao.arquivos["coruja.log"] = "diario";
    c.cartao.arquivos["infracoes.log"] = "multas";
    c.servidor.mente_no_crc = true;

    EXPECT_EQ(c.roda(), ResultadoRemessa::FalhaAoEnviar);
    // Os dois foram TENTADOS, mesmo o primeiro tendo falhado.
    EXPECT_EQ(c.servidor.recebidos.size(), 2u);
    EXPECT_EQ(c.falhados, 2u);
}

TEST(Remessa, entregue_sem_apagar_continua_sendo_entregue) {
    // Retido não é falha: o conteúdo está no servidor. Chamar isto de
    // `Parcial` mandaria o usuário tentar de novo um envio que deu certo.
    Cenario c;
    c.cartao.arquivos["coruja.log"] = "diario";
    c.cartao.arquivos["infracoes.log"] = "multas";
    c.cartao.falha_ao_remover = true;
    EXPECT_EQ(c.roda(), ResultadoRemessa::Enviada);
    EXPECT_EQ(c.entregues, 2u);
    EXPECT_EQ(c.retidos, 2u);
    EXPECT_EQ(c.falhados, 0u);
}

TEST(Remessa, desconecta_mesmo_quando_tudo_da_errado) {
    // Rádio ligado consome e não serve mais a ninguém.
    Cenario c;
    c.cartao.arquivos["coruja.log"] = "dado";
    c.servidor.erro_no_envio = ErroHttp::NaoIniciou;
    c.roda();
    EXPECT_EQ(c.rede.desconectou, 1);
}

TEST(Remessa, nao_abrir_o_arquivo_nao_derruba_a_remessa_inteira) {
    Cenario c;
    c.cartao.arquivos["coruja.log"] = "dado";
    c.cartao.falha_ao_abrir = true;
    EXPECT_EQ(c.roda(), ResultadoRemessa::FalhaAoEnviar);
    EXPECT_EQ(c.rede.desconectou, 1);
}

TEST(Remessa, nao_rebobinar_nao_apaga) {
    Cenario c;
    c.cartao.arquivos["coruja.log"] = "dado";
    c.cartao.falha_ao_rebobinar = true;
    EXPECT_EQ(c.roda(), ResultadoRemessa::FalhaAoEnviar);
    EXPECT_TRUE(c.cartao.apagados.empty());
}

// ================================================================ descrições

TEST(Remessa, todo_resultado_tem_texto_curto_que_cabe_na_faixa) {
    // A faixa da tela tem 26 caracteres. Um texto maior aparece cortado e
    // diz outra coisa.
    for (auto r : {ResultadoRemessa::Enviada, ResultadoRemessa::NadaAEnviar,
                   ResultadoRemessa::Parcial, ResultadoRemessa::SemConfiguracao,
                   ResultadoRemessa::FalhaDeRede, ResultadoRemessa::FalhaDeCartao,
                   ResultadoRemessa::FalhaAoEnviar}) {
        EXPECT_LE(std::strlen(descreve_curto(r)), 26u) << descreve(r);
        EXPECT_GT(std::strlen(descreve_curto(r)), 0u);
    }
}

TEST(Remessa, so_os_dois_desfechos_bons_nao_sao_falha) {
    EXPECT_FALSE(e_falha(ResultadoRemessa::Enviada));
    EXPECT_FALSE(e_falha(ResultadoRemessa::NadaAEnviar));
    EXPECT_TRUE(e_falha(ResultadoRemessa::Parcial));
    EXPECT_TRUE(e_falha(ResultadoRemessa::SemConfiguracao));
    EXPECT_TRUE(e_falha(ResultadoRemessa::FalhaDeRede));
    EXPECT_TRUE(e_falha(ResultadoRemessa::FalhaDeCartao));
    EXPECT_TRUE(e_falha(ResultadoRemessa::FalhaAoEnviar));
}


// ================== a remessa sobre o CARTAO DE VERDADE ==================
//
// Tudo acima roda contra um `CartaoFalso` escrito a mao, e e por isso que a
// suite ficou verde enquanto o envio falhava no aparelho tres vezes seguidas.
// Aqui o `RemessaDados` fala com o `CartaoSd` DE PRODUCAO, compilado contra o
// duble de FatFs -- a mesma montagem, os mesmos dois descritores, a mesma
// sondagem de volume. E a unica forma de exercitar o que o aparelho executa.

struct CenarioCartaoReal {
    teste::FatFsFalso& fs = teste::FatFsFalso::instancia();
    CartaoSd          cartao;
    RedeFalsa         rede;
    ServidorFalso     servidor;
    teste::LoggerMock log;
    Configuracao      cfg = cfg_valida();

    CenarioCartaoReal() {
        fs.reinicia();
        fs.volumes[0].monta = true;
        // A forma EXATA do coruja.cfg do aparelho: https, e SEM barra final.
        std::snprintf(cfg.url_envio, sizeof cfg.url_envio,
                      "https://coruja.bpldev.com/envio");
    }

    void poe(const std::string& nome, const std::string& conteudo) {
        fs.volumes[0].arquivos[nome] = conteudo;
    }

    ResultadoRemessa roda() {
        RemessaDados r(cartao, rede, servidor, nullptr, nullptr);
        auto saida = r.executa(cfg, log);
        entregues = r.entregues();
        falhados = r.falhados();
        retidos = r.retidos();
        return saida;
    }
    unsigned entregues = 0, falhados = 0, retidos = 0;
};

TEST(RemessaComCartaoReal, uma_viagem_sobe_e_e_apagada) {
    CenarioCartaoReal c;
    c.poe("20261006_123858.log", "linha de viagem\n");

    EXPECT_EQ(c.roda(), ResultadoRemessa::Enviada);
    EXPECT_EQ(c.servidor.recebidos["20261006_123858.log"], "linha de viagem\n");
    EXPECT_FALSE(c.fs.existe(0, "20261006_123858.log"));
    EXPECT_EQ(c.falhados, 0u);
}

TEST(RemessaComCartaoReal, o_conjunto_do_aparelho_sobe_inteiro) {
    // Os arquivos que estavam no cartao em 07/10/2026, com os tamanhos reais.
    CenarioCartaoReal c;
    c.poe("20261004_225259.log", std::string(101, 'a'));
    c.poe("20261004_225323.log", std::string(251, 'b'));
    c.poe("20261006_123858.log", std::string(1176, 'c'));
    c.poe("coruja.log", std::string(32108, 'd'));
    c.poe("radares.bin", std::string(220064, 'x'));   // nao sobe
    c.poe("coruja.cfg", "nao sobe");

    EXPECT_EQ(c.roda(), ResultadoRemessa::Enviada);
    EXPECT_EQ(c.entregues, 4u);
    EXPECT_EQ(c.falhados, 0u);
    EXPECT_TRUE(c.fs.existe(0, "radares.bin")) << "subiu o que nao devia";
    EXPECT_TRUE(c.fs.existe(0, "coruja.cfg"));
    EXPECT_EQ(c.servidor.recebidos["coruja.log"].size(), 32108u);
}

TEST(RemessaComCartaoReal, o_caminho_montado_leva_a_barra_e_o_nome) {
    // `url_envio` sem barra final + nome do arquivo. Um "/enviocoruja.log"
    // nao chegaria a lugar nenhum, e o servidor nunca veria a requisicao --
    // que e exatamente o sintoma observado.
    CenarioCartaoReal c;
    c.poe("20261006_123858.log", "x");
    ASSERT_EQ(c.roda(), ResultadoRemessa::Enviada);
    ASSERT_FALSE(c.servidor.caminhos.empty());
    EXPECT_EQ(c.servidor.caminhos[0], "/envio/20261006_123858.log");
}

}  // namespace
