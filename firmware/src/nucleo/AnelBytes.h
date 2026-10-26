#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Bytes da serial entre a interrupção e o laço.
///
/// 🔴 **Existe por causa da medida de 26/10/2026.** O FIFO de hardware do
/// UART tem 32 bytes e transborda em **2,8 ms** a 115200. O laço principal
/// só o drenava entre uma volta e outra, e um redesenho da velocidade
/// bloqueia o SPI por ~53 ms — dezenove vezes o que o FIFO aguenta. O
/// resultado medido no diário: taxa de fixes em 3,7 Hz em vez de 4,0, com
/// quedas a 2,7 Hz, "com posição presente" — o receptor entregava e nós
/// jogávamos fora.
///
/// Com o anel, o tempo do laço deixa de importar: a interrupção despeja aqui
/// e o laço recolhe quando puder.
///
/// ## Um produtor, um consumidor, sem trava
///
/// A interrupção só escreve; o laço só lê. Cada um mexe no **seu** índice, e
/// por isso não há disputa — é o padrão SPSC. Os índices são atômicos porque
/// a interrupção corta o laço em qualquer ponto, e um índice lido pela
/// metade faria o consumidor enxergar bytes que ainda não existem.
///
/// ⚠️ **Nunca sobrescreve o que não foi lido.** Com o anel cheio o byte novo
/// é descartado e `perdidos()` conta. Sobrescrever corromperia a sentença
/// que o laço está no meio de ler — trocaria perda visível por lixo que
/// passa pelo parser.
class AnelBytes {
public:
    /// 1 KiB são ~89 ms de linha a 115200: folga de quase duas vezes sobre o
    /// pior bloqueio conhecido (o redesenho completo da faixa do número).
    static constexpr std::size_t kTamanho = 1024;

    /// Chamado pela INTERRUPÇÃO. Devolve falso quando o anel está cheio.
    bool escreve(std::uint8_t b) {
        const std::size_t fim = fim_.load(std::memory_order_relaxed);
        const std::size_t proximo = (fim + 1) % kTamanho;
        if (proximo == inicio_.load(std::memory_order_acquire)) {
            perdidos_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        dados_[fim] = b;
        fim_.store(proximo, std::memory_order_release);
        return true;
    }

    /// Chamado pelo LAÇO. Devolve quantos bytes saíram.
    std::size_t le(std::uint8_t* destino, std::size_t capacidade) {
        if (destino == nullptr) { return 0; }
        std::size_t n = 0;
        std::size_t ini = inicio_.load(std::memory_order_relaxed);
        while (n < capacidade && ini != fim_.load(std::memory_order_acquire)) {
            destino[n++] = dados_[ini];
            ini = (ini + 1) % kTamanho;
        }
        inicio_.store(ini, std::memory_order_release);
        return n;
    }

    std::size_t quantos() const {
        const std::size_t f = fim_.load(std::memory_order_acquire);
        const std::size_t i = inicio_.load(std::memory_order_acquire);
        return (f + kTamanho - i) % kTamanho;
    }

    /// Bytes descartados por anel cheio, desde sempre.
    ///
    /// **É a medida que faltava.** Diferente de zero significa que o laço
    /// ficou longe demais da serial, e diz em bytes o tamanho do atraso.
    std::uint32_t perdidos() const {
        return perdidos_.load(std::memory_order_relaxed);
    }

    void zera_perdidos() { perdidos_.store(0, std::memory_order_relaxed); }

private:
    std::uint8_t dados_[kTamanho] = {};
    std::atomic<std::size_t> inicio_{0};   ///< o laço avança
    std::atomic<std::size_t> fim_{0};      ///< a interrupção avança
    std::atomic<std::uint32_t> perdidos_{0};
};

}  // namespace coruja
