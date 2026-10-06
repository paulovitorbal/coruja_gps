#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Alocador sobre um bloco de memória emprestado.
///
/// **Existe para o TLS caber sem crescer o aparelho.** O handshake do mbedTLS
/// quer dezenas de KiB em dois buffers de registro, e os ~173 KiB livres do
/// RP2350 não deixam folga confortável. Ao lado deles há 280 KiB parados: o
/// vetor da base de radares, que durante uma atualização está **ocioso** —
/// ninguém pode alertar enquanto a rede trabalha. Emprestar essa área é
/// trocar memória que não está em uso por memória que falta.
///
/// **Primeiro ajuste com junção de vizinhos, e não um ponteiro que só avança.**
/// Um alocador que ignora o `free` seria dez linhas, e bastaria se o mbedTLS
/// alocasse tudo de uma vez — ele não faz isso: aloca e libera ao longo do
/// handshake, e a área se esgotaria no meio de uma sessão que caberia.
///
/// Não é de uso geral e não quer ser: sem concorrência (o `cyw43_arch` deste
/// firmware está em modo *poll*, e tudo roda no laço principal) e sem
/// realocação.
class ArenaMemoria {
public:
    /// Alinhamento de toda alocação. Oito bytes cobre `double` e `int64_t`,
    /// que é o que o ARM Cortex-M33 pede no pior caso.
    static constexpr std::size_t kAlinhamento = 8;

    ArenaMemoria() = default;
    ArenaMemoria(void* memoria, std::size_t bytes) { adota(memoria, bytes); }

    /// Passa a usar `memoria`. Esquece o que havia antes — chamar com
    /// alocações vivas é erro de quem chama, e o `em_uso()` o denuncia.
    void adota(void* memoria, std::size_t bytes);

    /// Devolve um bloco **zerado** de `quantos * tamanho` bytes, ou nulo.
    ///
    /// Assinatura de `calloc` porque é isso que o mbedTLS instala
    /// (`mbedtls_platform_set_calloc_free`). Zerar não é cortesia: o mbedTLS
    /// conta com isso, e material de chave sobre lixo de uma sessão anterior
    /// seria o pior defeito possível aqui.
    void* aloca(std::size_t quantos, std::size_t tamanho);

    /// Libera. Ponteiro nulo é no-op, como `free`. Ponteiro que não saiu
    /// daqui é ignorado — e contado em `invalidos()`.
    void libera(void* ponteiro);

    std::size_t capacidade() const { return capacidade_; }
    std::size_t em_uso() const { return em_uso_; }
    /// O maior `em_uso()` já visto. É o número que diz se a área tem folga.
    std::size_t pico() const { return pico_; }
    /// Quantas vezes faltou espaço. Zero é o que se espera; mais que zero
    /// explica um handshake que falhou sem motivo aparente.
    std::size_t faltas() const { return faltas_; }
    std::size_t invalidos() const { return invalidos_; }

private:
    /// Cabeçalho de bloco, imediatamente antes da área entregue.
    struct Bloco {
        std::size_t tamanho;  ///< bytes úteis, já alinhados
        Bloco*      proximo;
        bool        livre;
    };

    void junta_vizinhos();

    Bloco*      primeiro_ = nullptr;
    std::size_t capacidade_ = 0;
    std::size_t em_uso_ = 0;
    std::size_t pico_ = 0;
    std::size_t faltas_ = 0;
    std::size_t invalidos_ = 0;
};

}  // namespace coruja
