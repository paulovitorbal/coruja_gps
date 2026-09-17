#pragma once
#include <vector>

#include "led/LedRgb.h"

namespace coruja::teste {

/// Mock de `LedRgb` que guarda o histórico de cores, para os testes afirmarem
/// sobre a sequência e não só sobre o estado final.
class LedRgbMock final : public LedRgb {
public:
    void define_cor(const Cor& cor) override {
        atual_ = cor;
        historico_.push_back(cor);
    }
    Cor cor_atual() const override { return atual_; }

    const std::vector<Cor>& historico() const { return historico_; }
    std::size_t escritas() const { return historico_.size(); }
    void limpa() { historico_.clear(); }

private:
    Cor              atual_{};
    std::vector<Cor> historico_;
};

}  // namespace coruja::teste
