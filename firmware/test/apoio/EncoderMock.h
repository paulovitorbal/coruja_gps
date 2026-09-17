#pragma once
#include <deque>
#include <initializer_list>

#include "encoder/Encoder.h"

namespace coruja::teste {

/// Mock de `Encoder` que entrega uma fila de eventos pré-programada.
class EncoderMock final : public Encoder {
public:
    EncoderMock() = default;
    EncoderMock(std::initializer_list<EventoEncoder> eventos)
        : fila_(eventos.begin(), eventos.end()) {}

    void enfileira(EventoEncoder e) { fila_.push_back(e); }

    EventoEncoder proximo_evento() override {
        if (fila_.empty()) {
            return EventoEncoder::Nenhum;
        }
        const auto e = fila_.front();
        fila_.pop_front();
        return e;
    }

    bool vazio() const { return fila_.empty(); }

private:
    std::deque<EventoEncoder> fila_;
};

}  // namespace coruja::teste
