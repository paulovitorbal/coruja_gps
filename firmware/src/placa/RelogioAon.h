#pragma once
#include "nucleo/SincronizadorHora.h"

namespace coruja {

/// O relógio que atravessa a ligação, sobre o temporizador do bloco POWMAN.
///
/// ⚠️ **O RP2350 não tem o RTC do RP2040.** Aquele periférico saiu do chip; o
/// `hardware/rtc.h` do SDK nem compila para esta família. O substituto é o
/// *always-on timer*, que conta segundos em vez de guardar calendário — e é
/// por isso que a conversão civil mora no `TempoUtc`, em código portável e
/// testado, em vez de vir das funções `_calendar()` do SDK (que arrastam o
/// `mktime` da libc junto).
///
/// ⚠️ **Não há bateria na placa.** Ele mantém a hora enquanto houver
/// alimentação, e o aparelho desliga com a ignição: todo boot começa do zero.
/// Por isso o `SincronizadorHora` roda por ligação, e não uma vez na vida.
class RelogioAon final : public RelogioPersistente {
public:
    /// Liga o temporizador. Idempotente.
    void inicia();

    std::int64_t agora_utc() const override;
    void define_utc(std::int64_t segundos) override;

private:
    bool iniciado_ = false;
};

}  // namespace coruja
