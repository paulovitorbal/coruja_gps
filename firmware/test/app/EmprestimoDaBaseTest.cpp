#include "app/EmprestimoDaBase.h"

#include <gtest/gtest.h>

#include <vector>

#include "apoio/LoggerMock.h"
#include <cstring>

#include "buzzer/Buzzer.h"
#include "gps/LeitorGps.h"
#include "led/LedRgb.h"

namespace {

using namespace coruja;

class LedBobo : public LedRgb {
public:
    void define_cor(const Cor& c) override { atual = c; }
    Cor cor_atual() const override { return atual; }
    Cor atual = cores::kApagado;
};

class BuzzerBobo : public Buzzer {
public:
    void define(bool l) override { ligado_ = l; }
    bool ligado() const override { return ligado_; }
    bool ligado_ = false;
};

/// Nunca entrega byte: sem fix, que é o estado do carro parado na garagem
/// durante uma atualização.
class UartMuda : public Uart {
public:
    void escreve(const std::uint8_t*, std::size_t) override {}
    void define_baud(std::uint32_t) override {}
    std::size_t le(std::uint8_t*, std::size_t) override { return 0; }
};

class RecarregadorFalso : public RecarregadorBase {
public:
    std::size_t devolve = 7;
    int         chamadas = 0;
    Ponto*      destino = nullptr;

    std::size_t recarrega(Logger&) override {
        ++chamadas;
        // Recarregar de verdade REESCREVE o vetor. Sem isto o teste não
        // distinguiria "recarregou" de "devolveu o lixo do handshake".
        if (destino != nullptr) {
            for (std::size_t i = 0; i < devolve; ++i) {
                destino[i] = Ponto{};
                destino[i].lat = -15.85F + static_cast<float>(i) * 0.001F;
            }
        }
        return devolve;
    }
};

struct Bancada {
    UartMuda          uart;
    LeitorGps         gps{uart};
    LedBobo           led;
    BuzzerBobo        buzzer;
    PilotoAlerta      piloto{gps, led, buzzer};
    RecarregadorFalso recarregador;
    teste::LoggerMock log;
    std::vector<Ponto> area{512};

    Bancada() {
        recarregador.destino = area.data();
        piloto.define_base(area.data(), area.size());
    }
};

// ====================================================== não alerta emprestado

TEST(EmprestimoDaBase, tira_a_base_do_piloto_enquanto_dura) {
    // O ponto inteiro: enquanto o mbedTLS escreve no vetor, o piloto não
    // pode estar lendo coordenadas de dentro de um handshake.
    Bancada b;
    ASSERT_TRUE(b.piloto.tem_base());
    {
        EmprestimoDaBase e(b.piloto, b.recarregador, b.area.data(),
                           b.area.size(), b.log);
        EXPECT_FALSE(b.piloto.tem_base()) << "o piloto ainda consulta a base";
    }
}

TEST(EmprestimoDaBase, o_piloto_nao_alerta_durante_o_emprestimo) {
    // Afirma o EFEITO, e não a bandeira: uma volta do laço sem base tem de
    // sair sem veredito de alerta.
    Bancada b;
    EmprestimoDaBase e(b.piloto, b.recarregador, b.area.data(), b.area.size(),
                       b.log);
    b.piloto.passo(1000);
    // `SemSinal` e o estado de alertas suspensos. Sem base, o `passo()` cai
    // no mesmo caminho de quando nao ha fix -- e nao ha o que afirmar sobre
    // a via nos dois casos.
    EXPECT_EQ(b.piloto.veredito().zona, Zona::SemSinal);
    EXPECT_FALSE(b.buzzer.ligado());
}

// ============================================================ a área rende

TEST(EmprestimoDaBase, a_area_emprestada_e_quase_todo_o_vetor) {
    Bancada b;
    EmprestimoDaBase e(b.piloto, b.recarregador, b.area.data(), b.area.size(),
                       b.log);
    const std::size_t bruto = b.area.size() * sizeof(Ponto);
    EXPECT_GT(e.bytes(), bruto - 64);
    EXPECT_LE(e.bytes(), bruto);
}

TEST(EmprestimoDaBase, a_arena_entrega_memoria_de_verdade) {
    Bancada b;
    EmprestimoDaBase e(b.piloto, b.recarregador, b.area.data(), b.area.size(),
                       b.log);
    void* p = e.arena().aloca(1, 1024);
    ASSERT_NE(p, nullptr);
    std::memset(p, 0x5A, 1024);
}

// ======================================================= devolve recarregado

TEST(EmprestimoDaBase, recarrega_antes_de_devolver_o_ponteiro) {
    // O empréstimo DESTRÓI a base. Devolver o ponteiro sem recarregar faria
    // o piloto alertar sobre restos de um handshake -- e a tela não teria
    // como denunciar isso.
    Bancada b;
    {
        EmprestimoDaBase e(b.piloto, b.recarregador, b.area.data(),
                           b.area.size(), b.log);
        // Suja a area, como o mbedTLS faria.
        std::memset(b.area.data(), 0xCC, b.area.size() * sizeof(Ponto));
    }
    EXPECT_EQ(b.recarregador.chamadas, 1);
    EXPECT_TRUE(b.piloto.tem_base());
    // A recarga reescreveu de verdade: o primeiro ponto nao e mais 0xCC.
    EXPECT_FLOAT_EQ(b.area[0].lat, -15.85F);
}

TEST(EmprestimoDaBase, recarga_que_falha_deixa_o_aparelho_SEM_base) {
    // Lado certo da assimetria: alertar com base duvidosa e pior que nao
    // alertar, porque o motorista confia no que ve.
    Bancada b;
    b.recarregador.devolve = 0;
    {
        EmprestimoDaBase e(b.piloto, b.recarregador, b.area.data(),
                           b.area.size(), b.log);
    }
    EXPECT_FALSE(b.piloto.tem_base());
    EXPECT_TRUE(b.log.contem(Nivel::Error, "SEM ALERTA"));
}

TEST(EmprestimoDaBase, a_recarga_acontece_mesmo_se_a_sessao_falhou) {
    // O destrutor roda de qualquer jeito. Antes, o `main.cpp` so recarregava
    // depois de um OTA BEM-SUCEDIDO -- o que, com emprestimo, deixaria o
    // vetor sujo apos uma falha.
    Bancada b;
    {
        EmprestimoDaBase e(b.piloto, b.recarregador, b.area.data(),
                           b.area.size(), b.log);
        e.arena().aloca(1, 4096);   // sessao comecou e nao terminou
    }
    EXPECT_EQ(b.recarregador.chamadas, 1);
    EXPECT_TRUE(b.piloto.tem_base());
}

TEST(EmprestimoDaBase, o_numero_de_pontos_recarregado_e_o_que_vale) {
    Bancada b;
    b.recarregador.devolve = 3;
    {
        EmprestimoDaBase e(b.piloto, b.recarregador, b.area.data(),
                           b.area.size(), b.log);
    }
    EXPECT_TRUE(b.piloto.tem_base());
    // Nao sobrou a contagem antiga de 512.
    EXPECT_EQ(b.recarregador.devolve, 3U);
}

// ================================================================ o log

TEST(EmprestimoDaBase, registra_o_pico_para_dizer_se_coube) {
    // E o unico numero que responde "o TLS cabe nesta area": sem ele, so se
    // descobre que nao cabia quando um handshake falha em campo.
    Bancada b;
    {
        EmprestimoDaBase e(b.piloto, b.recarregador, b.area.data(),
                           b.area.size(), b.log);
        e.arena().aloca(1, 16 * 1024);
    }
    EXPECT_TRUE(b.log.contem(Nivel::Info, "pico de uso"));
}

TEST(EmprestimoDaBase, falta_de_memoria_vira_aviso) {
    Bancada b;
    {
        EmprestimoDaBase e(b.piloto, b.recarregador, b.area.data(),
                           b.area.size(), b.log);
        e.arena().aloca(1, 10 * 1024 * 1024);  // nao cabe
    }
    EXPECT_TRUE(b.log.contem(Nivel::Warning, "faltou memoria"));
}

}  // namespace
