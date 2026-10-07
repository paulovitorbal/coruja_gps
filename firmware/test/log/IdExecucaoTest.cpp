#include "log/IdExecucao.h"

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

namespace {

using namespace coruja;

std::string id_de(std::uint64_t sorteio) {
    char buf[kTamIdExecucao + 1] = {};
    monta_id_execucao(sorteio, buf);
    return buf;
}

TEST(IdExecucao, tem_o_tamanho_combinado_e_termina) {
    char buf[kTamIdExecucao + 2];
    std::memset(buf, 'Z', sizeof buf);
    monta_id_execucao(0x123456789ABCDEF0ULL, buf);

    EXPECT_EQ(std::strlen(buf), kTamIdExecucao);
    EXPECT_EQ(buf[kTamIdExecucao], '\0') << "sem terminador vira lixo no log";
    EXPECT_EQ(buf[kTamIdExecucao + 1], 'Z') << "escreveu alem do destino";
}

TEST(IdExecucao, so_usa_o_alfabeto_declarado) {
    const std::string alfabeto = kAlfabetoId;
    for (std::uint64_t s : {0ULL, 1ULL, 42ULL, 0xFFFFFFFFFFFFFFFFULL,
                            0x0123456789ABCDEFULL}) {
        for (char c : id_de(s)) {
            EXPECT_NE(alfabeto.find(c), std::string::npos)
                << "caractere '" << c << "' fora do alfabeto";
        }
    }
}

TEST(IdExecucao, nao_usa_os_caracteres_que_se_leem_errado) {
    // O identificador existe para alguém LER e comparar, muitas vezes de olho
    // ou dizendo em voz alta. `l1` e `O0` são onde a leitura erra, e um
    // identificador lido errado manda procurar na execução errada.
    const std::string alfabeto = kAlfabetoId;
    for (char ruim : {'0', '1', 'I', 'O', 'l'}) {
        EXPECT_EQ(alfabeto.find(ruim), std::string::npos)
            << "o alfabeto contem '" << ruim << "'";
    }
}

TEST(IdExecucao, sorteios_diferentes_dao_identificadores_diferentes) {
    std::set<std::string> vistos;
    for (std::uint64_t s = 0; s < 5000; ++s) {
        vistos.insert(id_de(s));
    }
    EXPECT_EQ(vistos.size(), 5000U) << "dois sorteios caíram no mesmo id";
}

TEST(IdExecucao, usa_o_alfabeto_INTEIRO) {
    // Um erro clássico aqui seria dividir errado e só alcançar parte do
    // alfabeto -- o identificador continuaria parecendo aleatório, com um
    // espaço de valores muito menor que o anunciado.
    std::set<char> usados;
    for (std::uint64_t s = 0; s < 200; ++s) {
        for (char c : id_de(s)) { usados.insert(c); }
    }
    EXPECT_EQ(usados.size(), sizeof kAlfabetoId - 1);
}

TEST(IdExecucao, o_mesmo_sorteio_da_o_mesmo_id) {
    EXPECT_EQ(id_de(0xDEADBEEFCAFEBABEULL), id_de(0xDEADBEEFCAFEBABEULL));
}

TEST(IdExecucao, ponteiro_nulo_nao_quebra) {
    monta_id_execucao(123, nullptr);
}

TEST(IdExecucao, comeca_vazio_e_define_fixa) {
    // Antes do boot definir, o prefixo e vazio -- melhor que um valor
    // inventado que pareceria uma execucao de verdade.
    define_id_execucao(0xA1B2C3D4E5F60718ULL);
    EXPECT_EQ(std::string(id_execucao()), id_de(0xA1B2C3D4E5F60718ULL));

    define_id_execucao(1);
    EXPECT_EQ(std::string(id_execucao()), id_de(1));
}

}  // namespace
