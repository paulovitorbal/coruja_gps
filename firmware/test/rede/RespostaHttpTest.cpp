#include "rede/RespostaHttp.h"

#include <gtest/gtest.h>

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

}  // namespace
