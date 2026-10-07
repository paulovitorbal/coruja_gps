#include "rede/RespostaHttp.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace {

using namespace coruja;

std::uint32_t status(const std::string& s) {
    return status_da_resposta(s.data(), s.size());
}

bool crc(const std::string& s, std::uint32_t* destino) {
    return le_crc_hex(s.data(), s.size(), destino);
}

// ============================================================ linha de status

TEST(StatusDaResposta, le_os_codigos_que_o_servidor_manda) {
    EXPECT_EQ(status("HTTP/1.1 201 Created\r\n\r\n"), 201u);
    EXPECT_EQ(status("HTTP/1.1 200 OK\r\n\r\n"), 200u);
    EXPECT_EQ(status("HTTP/1.1 401 Unauthorized\r\n\r\n"), 401u);
    EXPECT_EQ(status("HTTP/1.1 404 Not Found\r\n\r\n"), 404u);
    EXPECT_EQ(status("HTTP/1.1 413 Request Entity Too Large\r\n\r\n"), 413u);
}

TEST(StatusDaResposta, aceita_qualquer_versao_do_protocolo) {
    EXPECT_EQ(status("HTTP/1.0 200 OK\r\n\r\n"), 200u);
}

TEST(StatusDaResposta, lixo_nao_vira_status) {
    // Um servidor que não é o esperado, ou uma conexão que entregou outra
    // coisa. Devolver zero faz o cliente tratar como falha -- inventar 200
    // faria o aparelho acreditar numa resposta que não houve.
    EXPECT_EQ(status(""), 0u);
    EXPECT_EQ(status("ola mundo"), 0u);
    EXPECT_EQ(status("HTTP/1.1"), 0u);
    EXPECT_EQ(status("HTTP/1.1 OK\r\n"), 0u);
    EXPECT_EQ(status("HTTP/1.1 2O1 Created\r\n"), 0u);  // letra O no lugar do zero
    EXPECT_EQ(status("<html>500</html>"), 0u);
    EXPECT_EQ(status_da_resposta(nullptr, 10), 0u);
}

TEST(StatusDaResposta, texto_com_a_forma_certa_e_protocolo_errado_nao_vale) {
    // Um portal cativo ou um servidor que não é o esperado pode devolver uma
    // linha com três dígitos no lugar certo. Sem conferir o `HTTP/`, isso
    // vira um "200 OK" que nunca houve.
    EXPECT_EQ(status("HTML/1.1 200 OK\r\n\r\n"), 0u);
    EXPECT_EQ(status("GARBAGE 200 OK\r\n\r\n"), 0u);
    EXPECT_EQ(status("portal 201 criado\r\n\r\n"), 0u);
}

TEST(StatusDaResposta, resposta_cortada_no_meio_do_codigo_nao_vale) {
    EXPECT_EQ(status("HTTP/1.1 20"), 0u);
}

// =================================================================== corpo

TEST(CorpoDaResposta, vem_depois_da_linha_em_branco) {
    const std::string r = "HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\n59921050\n";
    std::size_t n = 0;
    const char* c = corpo_da_resposta(r.data(), r.size(), &n);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(std::string(c, n), "59921050\n");
}

TEST(CorpoDaResposta, aceita_separador_sem_o_retorno_de_carro) {
    // Um proxy no caminho pode normalizar. Recusar por isso seria falhar por
    // uma diferença que não muda nada.
    const std::string r = "HTTP/1.1 200 OK\nX: 1\n\n59921050\n";
    std::size_t n = 0;
    const char* c = corpo_da_resposta(r.data(), r.size(), &n);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(std::string(c, n), "59921050\n");
}

TEST(CorpoDaResposta, sem_linha_em_branco_nao_ha_corpo) {
    // Resposta cortada no meio dos cabeçalhos: os bytes seguintes PODEM ser
    // mais cabeçalho, e lê-los como corpo produziria um "CRC" inventado.
    const std::string r = "HTTP/1.1 200 OK\r\nContent-Length: 9\r\n";
    std::size_t n = 0;
    EXPECT_EQ(corpo_da_resposta(r.data(), r.size(), &n), nullptr);
}

TEST(CorpoDaResposta, corpo_vazio_e_um_corpo) {
    const std::string r = "HTTP/1.1 201 Created\r\n\r\n";
    std::size_t n = 1;
    const char* c = corpo_da_resposta(r.data(), r.size(), &n);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(n, 0u);
}

// ===================================================================== CRC

TEST(LeCrcHex, le_os_oito_digitos) {
    std::uint32_t v = 0;
    ASSERT_TRUE(crc("59921050", &v));
    EXPECT_EQ(v, 0x59921050u);
}

TEST(LeCrcHex, aceita_maiuscula_e_minuscula) {
    std::uint32_t a = 0, b = 0;
    ASSERT_TRUE(crc("deadbeef", &a));
    ASSERT_TRUE(crc("DEADBEEF", &b));
    EXPECT_EQ(a, 0xDEADBEEFu);
    EXPECT_EQ(a, b);
}

TEST(LeCrcHex, tolera_branco_em_volta) {
    // O servidor manda "%08x\n". A quebra de linha não pode invalidar.
    std::uint32_t v = 0;
    ASSERT_TRUE(crc("59921050\n", &v));
    EXPECT_EQ(v, 0x59921050u);
    ASSERT_TRUE(crc("  00000001\r\n", &v));
    EXPECT_EQ(v, 1u);
}

TEST(LeCrcHex, zeros_a_esquerda_sao_significativos) {
    std::uint32_t v = 0xFFFFFFFF;
    ASSERT_TRUE(crc("0000000a", &v));
    EXPECT_EQ(v, 10u);
}

TEST(LeCrcHex, o_maior_valor_cabe) {
    std::uint32_t v = 0;
    ASSERT_TRUE(crc("ffffffff", &v));
    EXPECT_EQ(v, 0xFFFFFFFFu);
}

TEST(LeCrcHex, nao_le_alem_do_tamanho_informado) {
    // O buffer da resposta é maior que o corpo, e os bytes seguintes são
    // cabeçalho ou lixo da conexão. Ler oito a partir do início sem olhar o
    // tamanho produziria um CRC perfeitamente formado e completamente
    // inventado -- que, batendo por acaso, apaga um arquivo.
    const char buffer[] = "1a2b3c4d5e6f";
    std::uint32_t v = 0;
    EXPECT_FALSE(le_crc_hex(buffer, 4, &v));
    EXPECT_FALSE(le_crc_hex(buffer, 7, &v));
    EXPECT_TRUE(le_crc_hex(buffer, 8, &v));
    EXPECT_EQ(v, 0x1A2B3C4Du);
}

TEST(LeCrcHex, menos_de_oito_digitos_e_recusado) {
    // O motivo de não usar `strtoul`: "1a2b" seria aceito e viraria 0x1A2B,
    // um número plausível e errado. Um CRC errado que por acaso bate autoriza
    // apagar o único exemplar de um arquivo.
    std::uint32_t v = 0;
    EXPECT_FALSE(crc("1a2b", &v));
    EXPECT_FALSE(crc("5992105", &v));
    EXPECT_FALSE(crc("", &v));
}

TEST(LeCrcHex, mais_de_oito_digitos_e_recusado) {
    std::uint32_t v = 0;
    EXPECT_FALSE(crc("599210501", &v));
    EXPECT_FALSE(crc("deadbeefcafe", &v));
}

TEST(LeCrcHex, caractere_que_nao_e_hexadecimal_recusa) {
    std::uint32_t v = 0;
    EXPECT_FALSE(crc("5992105g", &v));
    EXPECT_FALSE(crc("nao e crc", &v));
    EXPECT_FALSE(crc("0x599210", &v));
}

TEST(LeCrcHex, html_de_pagina_de_erro_nao_vira_crc) {
    // O caso que mais importa: um proxy ou portal cativo devolvendo uma
    // página. Oito caracteres quaisquer dela não podem virar um número que
    // autoriza apagar arquivo.
    std::uint32_t v = 0;
    EXPECT_FALSE(crc("<html><body>erro</body></html>", &v));
    EXPECT_FALSE(crc("Not Found", &v));
}

TEST(LeCrcHex, recusa_nao_toca_no_destino) {
    // Quem chama pode ter deixado algo lá. Escrever um valor parcial numa
    // leitura que falhou seria a pior combinação -- e o caso que importa é o
    // que falha NO MEIO, com sete dígitos já convertidos.
    std::uint32_t v = 0x12345678;
    EXPECT_FALSE(crc("nao", &v));
    EXPECT_EQ(v, 0x12345678u);

    EXPECT_FALSE(crc("5992105g", &v));
    EXPECT_EQ(v, 0x12345678u);

    EXPECT_FALSE(crc("599210501", &v));
    EXPECT_EQ(v, 0x12345678u);
}

TEST(LeCrcHex, ponteiro_nulo_nao_quebra) {
    std::uint32_t v = 0;
    EXPECT_FALSE(le_crc_hex(nullptr, 8, &v));
    EXPECT_FALSE(le_crc_hex("59921050", 8, nullptr));
}


// ===================================================================
// LeitorRespostaHttp: a resposta que chega aos pedaços, pela rede
// ===================================================================

struct Coletor {
    std::string corpo;
    static void ao_receber(void* ctx, const std::uint8_t* b, std::size_t n) {
        static_cast<Coletor*>(ctx)->corpo.append(
            reinterpret_cast<const char*>(b), n);
    }
};

/// Alimenta a resposta inteira em blocos de `passo` bytes.
///
/// O tamanho do bloco é parâmetro de propósito: a rede entrega onde quiser, e
/// um leitor que só funcione com a resposta inteira de uma vez funciona no
/// teste e falha no fio.
bool alimenta_em_blocos(LeitorRespostaHttp& leitor, const std::string& bruto,
                        std::size_t passo, Coletor* c) {
    for (std::size_t i = 0; i < bruto.size(); i += passo) {
        const std::size_t n = std::min(passo, bruto.size() - i);
        if (!leitor.alimenta(
                reinterpret_cast<const std::uint8_t*>(bruto.data() + i), n,
                Coletor::ao_receber, c)) {
            return false;
        }
    }
    return true;
}

TEST(LeitorResposta, corpo_com_content_length) {
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\nola, mundo!";
    for (std::size_t passo : {1U, 3U, 7U, 64U, 999U}) {
        LeitorRespostaHttp leitor;
        Coletor c;
        ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, passo, &c)) << passo;
        EXPECT_EQ(leitor.status(), 200u) << passo;
        EXPECT_EQ(c.corpo, "ola, mundo!") << passo;
        EXPECT_TRUE(leitor.completa()) << passo;
        EXPECT_EQ(leitor.recebidos(), 11u) << passo;
        EXPECT_EQ(leitor.content_length(), 11);
    }
}

TEST(LeitorResposta, corpo_em_pedacos) {
    // O Cloudflare decide sozinho entre Content-Length e pedaços, e a escolha
    // muda com compressão e versão. Um cliente que só entendesse o primeiro
    // gravaria os cabeçalhos de pedaço dentro do radares.bin.
    const std::string bruto =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n\r\n"
        "5\r\nabcde\r\n"
        "3\r\nfgh\r\n"
        "0\r\n\r\n";
    for (std::size_t passo : {1U, 2U, 5U, 13U, 999U}) {
        LeitorRespostaHttp leitor;
        Coletor c;
        ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, passo, &c)) << passo;
        EXPECT_EQ(c.corpo, "abcdefgh") << "passo " << passo;
        EXPECT_TRUE(leitor.completa()) << passo;
        EXPECT_EQ(leitor.recebidos(), 8u) << passo;
    }
}

TEST(LeitorResposta, pedaco_grande_em_hexadecimal) {
    // `1000` é 4096, não mil. Ler como decimal daria um corpo truncado que
    // ainda assim "funcionaria" -- e o CRC acusaria sem dizer por quê.
    std::string dados(4096, 'x');
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "1000\r\n" + dados + "\r\n0\r\n\r\n";
    LeitorRespostaHttp leitor;
    Coletor c;
    ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, 100, &c));
    EXPECT_EQ(c.corpo.size(), 4096u);
    EXPECT_TRUE(leitor.completa());
}

TEST(LeitorResposta, tamanho_de_pedaco_com_letra_hexadecimal) {
    // Os tamanhos dos outros testes são só dígitos, e isso os deixa cegos: a
    // conta `valor*16 + digito` dá o mesmo resultado para "1000" quer os
    // dígitos a-f sejam reconhecidos, quer não. Só uma LETRA distingue
    // hexadecimal de decimal -- uma campanha de mutação mostrou isso.
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "a\r\n0123456789\r\n"
        "F\r\n" + std::string(15, 'x') + "\r\n"
        "0\r\n\r\n";
    LeitorRespostaHttp leitor;
    Coletor c;
    ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, 3, &c));
    EXPECT_EQ(c.corpo, "0123456789" + std::string(15, 'x'));
    EXPECT_EQ(leitor.recebidos(), 25u);
    EXPECT_TRUE(leitor.completa());
}

TEST(LeitorResposta, pedaco_de_255_bytes) {
    // `ff` em minúscula, o outro lado da mesma cegueira.
    const std::string dados(255, 'z');
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "ff\r\n" + dados + "\r\n0\r\n\r\n";
    LeitorRespostaHttp leitor;
    Coletor c;
    ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, 17, &c));
    EXPECT_EQ(c.corpo, dados);
}

TEST(LeitorResposta, content_length_sem_digito_nao_vira_corpo_vazio) {
    // Um proxy mandando algo estranho. Tratar como zero DESCARTARIA o corpo
    // em silêncio -- e o radares.bin chegaria vazio sem ninguém saber por
    // quê. Sem dígito, o tamanho é desconhecido: lê até fechar.
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\ncorpo de verdade";
    LeitorRespostaHttp leitor;
    Coletor c;
    ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, 6, &c));
    EXPECT_EQ(leitor.content_length(), -1);
    EXPECT_EQ(c.corpo, "corpo de verdade");
}

TEST(LeitorResposta, content_length_vazio_tambem) {
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nContent-Length:\r\n\r\nabc";
    LeitorRespostaHttp leitor;
    Coletor c;
    ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, 4, &c));
    EXPECT_EQ(leitor.content_length(), -1);
    EXPECT_EQ(c.corpo, "abc");
}

TEST(LeitorResposta, pedaco_com_extensao_e_aceito) {
    // `; nome=valor` depois do tamanho é legal e ninguém usa -- até alguém
    // usar.
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "4;qualquer=coisa\r\nabcd\r\n0\r\n\r\n";
    LeitorRespostaHttp leitor;
    Coletor c;
    ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, 3, &c));
    EXPECT_EQ(c.corpo, "abcd");
}

TEST(LeitorResposta, cabecalho_em_minuscula_tambem_vale) {
    // O Cloudflare manda em minúscula; o servidor de referência, capitalizado.
    // Comparar byte a byte funcionaria contra um e falharia contra o outro.
    const std::string bruto =
        "HTTP/1.1 200 OK\r\ncontent-length: 3\r\n\r\nabc";
    LeitorRespostaHttp leitor;
    Coletor c;
    ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, 5, &c));
    EXPECT_EQ(c.corpo, "abc");
    EXPECT_EQ(leitor.content_length(), 3);
}

TEST(LeitorResposta, chunked_em_minuscula_e_com_outras_codificacoes) {
    const std::string bruto =
        "HTTP/1.1 200 OK\r\ntransfer-encoding: gzip, chunked\r\n\r\n"
        "2\r\noi\r\n0\r\n\r\n";
    LeitorRespostaHttp leitor;
    Coletor c;
    ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, 4, &c));
    EXPECT_EQ(c.corpo, "oi");
}

TEST(LeitorResposta, sem_content_length_nem_pedacos_vai_ate_fechar) {
    // HTTP/1.0 e algumas respostas de erro. O fim é o fechamento da conexão,
    // que quem sabe é a camada de transporte -- por isso `completa()` é
    // falso aqui, e isso não é erro.
    const std::string bruto = "HTTP/1.1 200 OK\r\nServer: x\r\n\r\ncorpo solto";
    LeitorRespostaHttp leitor;
    Coletor c;
    ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, 6, &c));
    EXPECT_EQ(c.corpo, "corpo solto");
    EXPECT_FALSE(leitor.completa());
    EXPECT_EQ(leitor.content_length(), -1);
}

TEST(LeitorResposta, corpo_vazio_com_content_length_zero) {
    const std::string bruto = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
    LeitorRespostaHttp leitor;
    Coletor c;
    ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, 7, &c));
    EXPECT_EQ(leitor.status(), 204u);
    EXPECT_TRUE(leitor.completa());
    EXPECT_TRUE(c.corpo.empty());
}

TEST(LeitorResposta, nao_entrega_corpo_alem_do_content_length) {
    // Byte a mais depois do corpo -- `keep-alive` mal fechado, ou lixo. Ele
    // não pode entrar no radares.bin.
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabcLIXO DEPOIS";
    LeitorRespostaHttp leitor;
    Coletor c;
    ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, 2, &c));
    EXPECT_EQ(c.corpo, "abc");
    EXPECT_EQ(leitor.recebidos(), 3u);
}

TEST(LeitorResposta, status_de_erro_e_lido_e_o_corpo_tambem) {
    const std::string bruto =
        "HTTP/1.1 401 Unauthorized\r\nContent-Length: 5\r\n\r\nnops!";
    LeitorRespostaHttp leitor;
    Coletor c;
    ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, 9, &c));
    EXPECT_EQ(leitor.status(), 401u);
    EXPECT_EQ(c.corpo, "nops!");
}

TEST(LeitorResposta, resposta_sem_linha_de_status_e_recusada) {
    const std::string bruto = "ISTO NAO E HTTP\r\n\r\ncorpo";
    LeitorRespostaHttp leitor;
    Coletor c;
    EXPECT_FALSE(alimenta_em_blocos(leitor, bruto, 4, &c));
}

TEST(LeitorResposta, cabecalho_sem_fim_e_recusado_e_nao_estoura) {
    // Um servidor hostil mandando cabeçalho infinito não pode consumir a
    // RAM do aparelho nem escrever além do buffer.
    const std::string bruto =
        "HTTP/1.1 200 OK\r\n" + std::string(8000, 'x') + "\r\n\r\n";
    LeitorRespostaHttp leitor;
    Coletor c;
    EXPECT_FALSE(alimenta_em_blocos(leitor, bruto, 128, &c));
}

TEST(LeitorResposta, tamanho_de_pedaco_invalido_e_recusado) {
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "zzz\r\nabc\r\n0\r\n\r\n";
    LeitorRespostaHttp leitor;
    Coletor c;
    EXPECT_FALSE(alimenta_em_blocos(leitor, bruto, 5, &c));
}

TEST(LeitorResposta, reiniciar_limpa_tudo) {
    LeitorRespostaHttp leitor;
    Coletor c;
    alimenta_em_blocos(leitor, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc",
                       4, &c);
    ASSERT_EQ(leitor.status(), 200u);

    leitor.reinicia();
    EXPECT_EQ(leitor.status(), 0u);
    EXPECT_EQ(leitor.recebidos(), 0u);
    EXPECT_FALSE(leitor.completa());
    EXPECT_EQ(leitor.content_length(), -1);

    Coletor c2;
    ASSERT_TRUE(alimenta_em_blocos(
        leitor, "HTTP/1.1 201 Created\r\nContent-Length: 2\r\n\r\noi", 3, &c2));
    EXPECT_EQ(leitor.status(), 201u);
    EXPECT_EQ(c2.corpo, "oi");
}

TEST(LeitorResposta, o_corpo_que_vem_colado_nos_cabecalhos_nao_se_perde) {
    // O caso mais comum na rede real: o primeiro segmento TCP traz os
    // cabeçalhos e o começo do corpo juntos.
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n0123456789";
    LeitorRespostaHttp leitor;
    Coletor c;
    ASSERT_TRUE(leitor.alimenta(
        reinterpret_cast<const std::uint8_t*>(bruto.data()), bruto.size(),
        Coletor::ao_receber, &c));
    EXPECT_EQ(c.corpo, "0123456789");
}


// --- a borda do buffer de cabecalhos --------------------------------------

/// Cabecalhos com exatamente `alvo` bytes, terminador incluido.
///
/// O tamanho sai de `kMaxCabecalhos`, e nao de um literal: mudar o teto nao
/// pode fazer o teste parar de exercitar a borda sem que ninguem perceba.
std::string cabecalhos_de(std::size_t alvo, std::size_t content_length) {
    std::string h = "HTTP/1.1 200 OK\r\nContent-Length: "
                    + std::to_string(content_length) + "\r\n";
    const std::size_t resta = alvo - h.size() - 2;  // o \r\n final da secao
    h += "X-Enchimento: " + std::string(resta - 16, 'p') + "\r\n";
    h += "\r\n";
    EXPECT_EQ(h.size(), alvo);
    return h;
}

TEST(LeitorResposta, corpo_que_atravessa_o_fim_do_buffer_de_cabecalhos) {
    // O bloco que a rede entregou atravessa o fim de `cabecalhos_`: parte
    // dele cabe, parte nao. O que nao coube e CORPO -- e sair do `alimenta`
    // aqui o descartaria em silencio. O sintoma seria um radares.bin curto
    // com CRC que nao bate, sem uma linha de log dizendo por que.
    constexpr std::size_t kCorpo = 300;
    const std::string bruto =
        cabecalhos_de(LeitorRespostaHttp::kMaxCabecalhos - 60, kCorpo)
        + std::string(kCorpo, 'y');

    LeitorRespostaHttp leitor;
    Coletor c;
    // De uma vez so, de proposito: e o unico jeito de o bloco ser maior que
    // o espaco livre em `cabecalhos_`.
    ASSERT_TRUE(leitor.alimenta(
        reinterpret_cast<const std::uint8_t*>(bruto.data()), bruto.size(),
        Coletor::ao_receber, &c));
    EXPECT_EQ(c.corpo.size(), kCorpo);
    EXPECT_EQ(leitor.recebidos(), kCorpo);
    EXPECT_TRUE(leitor.completa());
}

TEST(LeitorResposta, cabecalho_que_enche_o_buffer_sem_terminar_e_recusado) {
    // O outro lado da mesma borda: aqui nao ha terminador nenhum, e encher o
    // buffer tem de ser recusa -- nao silencio.
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nX-Enchimento: "
        + std::string(LeitorRespostaHttp::kMaxCabecalhos + 100, 'p');
    LeitorRespostaHttp leitor;
    Coletor c;
    EXPECT_FALSE(alimenta_em_blocos(leitor, bruto, 512, &c));
}

// --- tetos ----------------------------------------------------------------

TEST(LeitorResposta, content_length_absurdo_e_recusado) {
    // Vinte digitos estouram um `long` de 32 bits, e estouro com sinal e
    // comportamento indefinido: o compilador pode apagar a verificacao que
    // viesse DEPOIS da conta. Por isso ela vem antes.
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999\r\n\r\nx";
    LeitorRespostaHttp leitor;
    Coletor c;
    EXPECT_FALSE(alimenta_em_blocos(leitor, bruto, 999, &c));
}

TEST(LeitorResposta, content_length_exatamente_no_teto_passa) {
    // 1 GiB cravado: o ultimo valor aceito. Sem este caso, um teto errado por
    // um ficaria invisivel -- o teste de cima passaria do mesmo jeito.
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nContent-Length: 1073741824\r\n\r\n";
    LeitorRespostaHttp leitor;
    Coletor c;
    ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, 999, &c));
    EXPECT_EQ(leitor.content_length(), LeitorRespostaHttp::kTetoCorpo);
}

TEST(LeitorResposta, content_length_um_acima_do_teto_recusa) {
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nContent-Length: 1073741825\r\n\r\n";
    LeitorRespostaHttp leitor;
    Coletor c;
    EXPECT_FALSE(alimenta_em_blocos(leitor, bruto, 999, &c));
}

TEST(LeitorResposta, tamanho_de_pedaco_absurdo_e_recusado) {
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "ffffffffffffffff\r\n";
    LeitorRespostaHttp leitor;
    Coletor c;
    EXPECT_FALSE(alimenta_em_blocos(leitor, bruto, 999, &c));
}

TEST(LeitorResposta, pedaco_um_acima_do_teto_recusa) {
    // `40000001` em hexadecimal e 2^30 + 1. O vizinho de baixo, `40000000`,
    // e aceito -- e e o par que prova que o teto esta no lugar certo.
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "40000001\r\n";
    LeitorRespostaHttp leitor;
    Coletor c;
    EXPECT_FALSE(alimenta_em_blocos(leitor, bruto, 999, &c));
}

TEST(LeitorResposta, pedaco_exatamente_no_teto_e_aceito) {
    const std::string bruto =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "40000000\r\nxxxx";
    LeitorRespostaHttp leitor;
    Coletor c;
    // Nao chega corpo nenhum perto disso, claro: o que se verifica e que o
    // leitor ACEITOU o tamanho e passou a esperar dados.
    ASSERT_TRUE(alimenta_em_blocos(leitor, bruto, 999, &c));
    EXPECT_EQ(c.corpo, "xxxx");
    EXPECT_FALSE(leitor.completa());
}

}  // namespace
