#pragma once
#include <cstdint>

namespace coruja {

/// Filtro de repique para a chave do encoder.
///
/// Lógica pura, com o tempo injetado: recebe o nível do pino e o instante, e
/// só reconhece a mudança depois de o nível ficar estável por `janela_ms`.
/// Injetar o tempo é o que torna o filtro testável — o teste avança o relógio
/// em vez de dormir.
///
/// O nível é o do pino, não "pressionado": a chave do KY-040 fecha para GND,
/// então o pino em nível baixo significa pressionado. A inversão fica em quem
/// lê o hardware, para que este filtro não precise saber a polaridade.
class AntiRepique {
public:
    explicit AntiRepique(std::uint32_t janela_ms = 5) : janela_ms_(janela_ms) {}

    /// Devolve `true` no instante em que a transição para nível ativo é
    /// confirmada — uma borda, não um estado, para que um botão mantido
    /// pressionado não gere uma enxurrada de eventos.
    bool amostra(bool ativo, std::uint32_t agora_ms);

    bool estavel() const { return estavel_; }

private:
    std::uint32_t janela_ms_;
    bool          estavel_      = false;
    bool          candidato_    = false;
    std::uint32_t desde_ms_     = 0;
    bool          aguardando_   = false;
};

}  // namespace coruja
