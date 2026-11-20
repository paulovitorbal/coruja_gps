#include "nucleo/PoliticaDoCao.h"

namespace coruja {

unsigned PoliticaDoCao::le_contagem() const {
    const std::uint32_t bruto = armazem_.le();
    // Sem a marca, e lixo: registrador de religamento a frio, ou de outro
    // firmware. Lixo nunca vira contagem -- 0xDEADBEEF seria 48879 mordidas.
    if ((bruto & kMascaraMarca) != kMarca) { return 0; }
    return bruto & kMascaraContagem;
}

void PoliticaDoCao::grava_contagem(unsigned n) {
    // Satura em vez de dar a volta: 65536 mordidas seguidas nao viram zero.
    const unsigned limitada = n > kMascaraContagem ? kMascaraContagem : n;
    armazem_.grava(kMarca | limitada);
}

PoliticaDoCao::Decisao PoliticaDoCao::decide_no_boot(bool veio_de_travamento) {
    // Qualquer boot que nao foi mordida -- religamento, `.uf2` -- zera.
    const unsigned n = veio_de_travamento ? le_contagem() + 1 : 0;
    // Grava sempre, inclusive o zero: limpa o lixo do religamento a frio.
    grava_contagem(n);

    Decisao d;
    d.mordidas_seguidas = n;
    d.arma = n < kMordidasSeguidasMax;
    armado_ = d.arma;
    zerou_ = false;
    viu_o_primeiro_passo_ = false;
    return d;
}

void PoliticaDoCao::passo(std::uint32_t agora_ms) {
    // Sem cao armado nao ha saude a provar: o contador fica como esta, e o
    // proximo boot (que nao sera mordida) o zera.
    if (!armado_ || zerou_) { return; }
    if (!viu_o_primeiro_passo_) {
        viu_o_primeiro_passo_ = true;
        inicio_ms_ = agora_ms;
        return;
    }
    // Subtracao sem sinal: correta na virada dos 32 bits.
    if (agora_ms - inicio_ms_ >= kSaudavelMs) {
        grava_contagem(0);
        zerou_ = true;
    }
}

}  // namespace coruja
