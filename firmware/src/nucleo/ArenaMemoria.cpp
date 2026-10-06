#include "nucleo/ArenaMemoria.h"

#include <cstring>

namespace coruja {
namespace {

std::size_t arredonda(std::size_t n, std::size_t para) {
    return (n + para - 1) / para * para;
}

}  // namespace

void ArenaMemoria::adota(void* memoria, std::size_t bytes) {
    primeiro_ = nullptr;
    capacidade_ = 0;
    em_uso_ = 0;
    pico_ = 0;
    faltas_ = 0;
    invalidos_ = 0;

    if (memoria == nullptr) { return; }

    // Alinha o inicio: o chamador pode entregar qualquer endereco, e um
    // cabecalho desalinhado derruba o Cortex-M33 em acesso de 64 bits.
    auto bruto = reinterpret_cast<std::uintptr_t>(memoria);
    const std::uintptr_t alinhado = arredonda(bruto, kAlinhamento);
    const std::size_t perdido = alinhado - bruto;
    if (bytes <= perdido + sizeof(Bloco) + kAlinhamento) { return; }

    bytes -= perdido;
    auto* cabeca = reinterpret_cast<Bloco*>(alinhado);
    cabeca->tamanho = (bytes - sizeof(Bloco)) / kAlinhamento * kAlinhamento;
    cabeca->proximo = nullptr;
    cabeca->livre = true;

    primeiro_ = cabeca;
    capacidade_ = cabeca->tamanho;
}

void* ArenaMemoria::aloca(std::size_t quantos, std::size_t tamanho) {
    if (primeiro_ == nullptr || quantos == 0 || tamanho == 0) {
        return nullptr;
    }
    // Multiplicacao que nao pode estourar em silencio: `quantos * tamanho`
    // dando a volta pediria poucos bytes e devolveria um ponteiro que quem
    // chama usaria como se fosse enorme.
    if (quantos > SIZE_MAX / tamanho) {
        ++faltas_;
        return nullptr;
    }
    const std::size_t pedido = arredonda(quantos * tamanho, kAlinhamento);

    for (Bloco* b = primeiro_; b != nullptr; b = b->proximo) {
        if (!b->livre || b->tamanho < pedido) { continue; }

        // Sobra que ainda comporta cabecalho e um minimo util vira bloco
        // novo. Sobra pequena demais fica junto, como folga interna --
        // parti-la so criaria um bloco que nunca serviria para nada.
        const std::size_t resto = b->tamanho - pedido;
        if (resto >= sizeof(Bloco) + kAlinhamento) {
            auto* novo = reinterpret_cast<Bloco*>(
                reinterpret_cast<std::uint8_t*>(b) + sizeof(Bloco) + pedido);
            novo->tamanho = resto - sizeof(Bloco);
            novo->proximo = b->proximo;
            novo->livre = true;
            b->proximo = novo;
            b->tamanho = pedido;
        }

        b->livre = false;
        em_uso_ += b->tamanho;
        if (em_uso_ > pico_) { pico_ = em_uso_; }

        auto* util = reinterpret_cast<std::uint8_t*>(b) + sizeof(Bloco);
        std::memset(util, 0, b->tamanho);
        return util;
    }

    ++faltas_;
    return nullptr;
}

void ArenaMemoria::libera(void* ponteiro) {
    if (ponteiro == nullptr) { return; }

    auto* alvo = reinterpret_cast<Bloco*>(
        reinterpret_cast<std::uint8_t*>(ponteiro) - sizeof(Bloco));

    // Percorre a lista para confirmar que o bloco e nosso. Confiar no
    // ponteiro seria aceitar escrever num cabecalho inventado, e isto roda
    // com memoria emprestada da base de radares.
    for (Bloco* b = primeiro_; b != nullptr; b = b->proximo) {
        if (b != alvo) { continue; }
        if (b->livre) {
            // Liberar duas vezes descontaria o tamanho duas vezes e deixaria
            // o `em_uso_` mentindo.
            ++invalidos_;
            return;
        }
        b->livre = true;
        em_uso_ -= b->tamanho;
        junta_vizinhos();
        return;
    }
    ++invalidos_;
}

void ArenaMemoria::junta_vizinhos() {
    // Sem isto, alocar e liberar em sequencia picaria a area em blocos
    // pequenos e livres que nenhum pedido grande aproveitaria -- e o
    // handshake pediria 16 KiB com 200 KiB livres em migalhas.
    for (Bloco* b = primeiro_; b != nullptr; b = b->proximo) {
        while (b->livre && b->proximo != nullptr && b->proximo->livre) {
            b->tamanho += sizeof(Bloco) + b->proximo->tamanho;
            b->proximo = b->proximo->proximo;
        }
    }
}

}  // namespace coruja
