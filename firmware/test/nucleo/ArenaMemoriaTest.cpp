#include "nucleo/ArenaMemoria.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

namespace {

using namespace coruja;

/// Área de trabalho alinhada, como o vetor da base seria.
struct Area {
    alignas(8) std::uint8_t bytes[64 * 1024] = {};
};

bool alinhado(const void* p) {
    return reinterpret_cast<std::uintptr_t>(p) % ArenaMemoria::kAlinhamento == 0;
}

bool tudo_zero(const void* p, std::size_t n) {
    const auto* b = static_cast<const std::uint8_t*>(p);
    return std::all_of(b, b + n, [](std::uint8_t x) { return x == 0; });
}

// ================================================================= básico

TEST(ArenaMemoria, entrega_memoria_utilizavel) {
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);

    void* p = arena.aloca(1, 100);
    ASSERT_NE(p, nullptr);
    std::memset(p, 0xAB, 100);   // tem de ser escrevível
    EXPECT_GE(arena.em_uso(), 100U);
}

TEST(ArenaMemoria, o_bloco_vem_zerado) {
    // O mbedTLS instala isto como `calloc` e CONTA com o zero. Material de
    // chave sobre lixo de uma sessão anterior seria o pior defeito daqui.
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);

    void* sujo = arena.aloca(1, 512);
    ASSERT_NE(sujo, nullptr);
    std::memset(sujo, 0xFF, 512);
    arena.libera(sujo);

    void* limpo = arena.aloca(1, 512);
    ASSERT_NE(limpo, nullptr);
    EXPECT_TRUE(tudo_zero(limpo, 512)) << "reaproveitou bloco sem zerar";
}

TEST(ArenaMemoria, todo_bloco_sai_alinhado) {
    // Cabeçalho ou bloco desalinhado derruba o Cortex-M33 em acesso de 64
    // bits. Tamanhos ímpares de propósito.
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);
    for (std::size_t n : {1U, 3U, 7U, 9U, 15U, 17U, 100U, 333U}) {
        void* p = arena.aloca(1, n);
        ASSERT_NE(p, nullptr) << n;
        EXPECT_TRUE(alinhado(p)) << n;
    }
}

TEST(ArenaMemoria, memoria_desalinhada_na_entrada_e_corrigida) {
    // Quem empresta pode entregar qualquer endereço.
    Area a;
    ArenaMemoria arena(a.bytes + 3, sizeof a.bytes - 3);
    void* p = arena.aloca(1, 64);
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(alinhado(p));
}

TEST(ArenaMemoria, blocos_diferentes_nao_se_sobrepoem) {
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);

    std::vector<std::pair<std::uint8_t*, std::size_t>> blocos;
    for (int i = 1; i <= 40; ++i) {
        const std::size_t n = static_cast<std::size_t>(i) * 37;
        auto* p = static_cast<std::uint8_t*>(arena.aloca(1, n));
        ASSERT_NE(p, nullptr) << i;
        std::memset(p, i, n);
        blocos.emplace_back(p, n);
    }
    // Se dois blocos se tocassem, o preenchimento de um teria apagado o
    // outro. Verifica o CONTEÚDO, não os endereços.
    for (std::size_t i = 0; i < blocos.size(); ++i) {
        const auto [p, n] = blocos[i];
        for (std::size_t j = 0; j < n; ++j) {
            ASSERT_EQ(p[j], static_cast<std::uint8_t>(i + 1))
                << "bloco " << i << " byte " << j;
        }
    }
}

// ============================================================ reutilização

TEST(ArenaMemoria, liberar_devolve_o_espaco) {
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);
    const std::size_t vazio = arena.em_uso();

    void* p = arena.aloca(1, 1024);
    ASSERT_NE(p, nullptr);
    EXPECT_GT(arena.em_uso(), vazio);
    arena.libera(p);
    EXPECT_EQ(arena.em_uso(), vazio);
}

TEST(ArenaMemoria, alocar_e_liberar_em_ciclo_nao_esgota) {
    // O que um alocador de ponteiro-que-só-avança NÃO faz, e o motivo de
    // este não ser um: o mbedTLS aloca e libera ao longo do handshake, e a
    // área se esgotaria no meio de uma sessão que caberia.
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);
    for (int i = 0; i < 5000; ++i) {
        void* p = arena.aloca(1, 4096);
        ASSERT_NE(p, nullptr) << "esgotou na volta " << i;
        arena.libera(p);
    }
    EXPECT_EQ(arena.faltas(), 0U);
    EXPECT_EQ(arena.em_uso(), 0U);
}

TEST(ArenaMemoria, vizinhos_livres_se_juntam_para_um_pedido_grande) {
    // Sem junção, alocar e liberar picaria a área em migalhas e um pedido de
    // 16 KiB falharia com 60 KiB livres.
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);

    std::vector<void*> pedacos;
    for (int i = 0; i < 30; ++i) {
        void* p = arena.aloca(1, 1024);
        ASSERT_NE(p, nullptr);
        pedacos.push_back(p);
    }
    for (void* p : pedacos) { arena.libera(p); }

    void* grande = arena.aloca(1, 24 * 1024);
    EXPECT_NE(grande, nullptr) << "a area nao se recompos";
}

TEST(ArenaMemoria, uma_so_passada_junta_tres_vizinhos_seguidos) {
    // O caso que a liberação em sequência NÃO alcança: ali cada `libera`
    // dispara uma passada nova, e passadas repetidas acabam juntando tudo
    // mesmo que cada uma junte só um par. Aqui a última liberação cria de
    // uma vez uma corrida de TRÊS blocos livres, e ela tem de se resolver
    // numa passada só -- senão sobram migalhas com a área quase vazia.
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);

    void* A = arena.aloca(1, 4096);
    void* B = arena.aloca(1, 4096);
    void* C = arena.aloca(1, 4096);
    void* D = arena.aloca(1, 4096);   // separa a corrida do resto da area
    ASSERT_NE(A, nullptr);
    ASSERT_NE(D, nullptr);

    // Tapa a cauda. Sem isto o pedido grande seria atendido pelo espaço que
    // sobra no fim, e o teste passaria com a junção quebrada -- foi o que
    // uma campanha de mutação mostrou.
    std::vector<void*> tampao;
    while (void* p = arena.aloca(1, 1024)) { tampao.push_back(p); }
    ASSERT_FALSE(tampao.empty());

    arena.libera(A);
    arena.libera(C);
    arena.libera(B);   // só agora A, B e C formam uma corrida livre

    // Os três juntos dão 12 KiB mais os dois cabeçalhos que sumiram na
    // junção. Dois deles, não: A+B sozinhos não chegam lá.
    EXPECT_NE(arena.aloca(1, 12 * 1024), nullptr)
        << "a juncao parou antes do terceiro vizinho";
}

TEST(ArenaMemoria, liberar_fora_de_ordem_tambem_junta) {
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);
    std::vector<void*> p;
    for (int i = 0; i < 20; ++i) { p.push_back(arena.aloca(1, 2048)); }
    // Da direita para a esquerda: o vizinho a juntar é o seguinte, não o
    // anterior.
    for (auto it = p.rbegin(); it != p.rend(); ++it) { arena.libera(*it); }
    EXPECT_NE(arena.aloca(1, 32 * 1024), nullptr);
}

// ================================================================ limites

TEST(ArenaMemoria, pedido_maior_que_a_area_falha_sem_quebrar) {
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);
    EXPECT_EQ(arena.aloca(1, sizeof a.bytes * 2), nullptr);
    EXPECT_EQ(arena.faltas(), 1U);
    // E a arena continua utilizável depois da recusa.
    EXPECT_NE(arena.aloca(1, 128), nullptr);
}

TEST(ArenaMemoria, multiplicacao_que_estouraria_e_recusada) {
    // `quantos * tamanho` dando a volta pediria poucos bytes e devolveria um
    // ponteiro que quem chama usaria como se fosse enorme -- escrita fora da
    // área, em memória emprestada da base de radares.
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);
    EXPECT_EQ(arena.aloca(SIZE_MAX / 2, 4), nullptr);
    EXPECT_EQ(arena.aloca(SIZE_MAX, SIZE_MAX), nullptr);
}

TEST(ArenaMemoria, pedido_zerado_devolve_nulo) {
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);
    EXPECT_EQ(arena.aloca(0, 10), nullptr);
    EXPECT_EQ(arena.aloca(10, 0), nullptr);
}

TEST(ArenaMemoria, arena_sem_memoria_nao_entrega_nada) {
    ArenaMemoria arena;
    EXPECT_EQ(arena.aloca(1, 8), nullptr);
    EXPECT_EQ(arena.capacidade(), 0U);
    arena.libera(nullptr);  // não pode explodir
}

TEST(ArenaMemoria, area_minuscula_nao_vira_arena_invalida) {
    std::uint8_t pouco[4] = {};
    ArenaMemoria arena(pouco, sizeof pouco);
    EXPECT_EQ(arena.capacidade(), 0U);
    EXPECT_EQ(arena.aloca(1, 1), nullptr);
}

// ============================================================== o que erra

TEST(ArenaMemoria, liberar_nulo_e_no_op_como_free) {
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);
    arena.libera(nullptr);
    EXPECT_EQ(arena.invalidos(), 0U);
}

TEST(ArenaMemoria, ponteiro_de_fora_e_recusado_e_nao_corrompe) {
    // Confiar no ponteiro seria aceitar escrever num cabeçalho inventado --
    // e isto roda sobre memória emprestada da base de radares.
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);
    void* bom = arena.aloca(1, 256);
    ASSERT_NE(bom, nullptr);
    const std::size_t antes = arena.em_uso();

    int fora = 0;
    arena.libera(&fora);
    EXPECT_EQ(arena.invalidos(), 1U);
    EXPECT_EQ(arena.em_uso(), antes) << "contabilidade mexeu";

    EXPECT_NE(arena.aloca(1, 256), nullptr) << "a arena ficou utilizavel";
}

TEST(ArenaMemoria, liberar_duas_vezes_nao_desconta_duas_vezes) {
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);
    void* p = arena.aloca(1, 512);
    const std::size_t com_um = arena.em_uso();
    ASSERT_GT(com_um, 0U);

    arena.libera(p);
    const std::size_t depois = arena.em_uso();
    arena.libera(p);
    EXPECT_EQ(arena.em_uso(), depois) << "o em_uso ficou mentindo";
    EXPECT_EQ(arena.invalidos(), 1U);
}

// ================================================================ medição

TEST(ArenaMemoria, o_pico_e_o_numero_que_diz_se_cabe) {
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);
    void* p1 = arena.aloca(1, 8 * 1024);
    void* p2 = arena.aloca(1, 8 * 1024);
    const std::size_t pico = arena.pico();
    EXPECT_GE(pico, 16U * 1024);

    arena.libera(p1);
    arena.libera(p2);
    EXPECT_EQ(arena.em_uso(), 0U);
    EXPECT_EQ(arena.pico(), pico) << "o pico nao pode cair";
}

TEST(ArenaMemoria, adotar_de_novo_zera_a_contabilidade) {
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);
    arena.aloca(1, 1024);
    arena.adota(a.bytes, sizeof a.bytes);
    EXPECT_EQ(arena.em_uso(), 0U);
    EXPECT_EQ(arena.pico(), 0U);
    EXPECT_EQ(arena.faltas(), 0U);
}

TEST(ArenaMemoria, a_capacidade_e_quase_toda_a_area) {
    // Um cabeçalho de perda, não mais. Se o desperdício crescer, o TLS pode
    // deixar de caber sem ninguém notar.
    Area a;
    ArenaMemoria arena(a.bytes, sizeof a.bytes);
    EXPECT_GT(arena.capacidade(), sizeof a.bytes - 64);
    EXPECT_LE(arena.capacidade(), sizeof a.bytes);
}

}  // namespace
