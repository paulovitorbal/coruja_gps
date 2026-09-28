#pragma once
#include <cstdint>

namespace coruja {

/// Pre-condicao de seguranca do RF05.1: veiculo parado.
constexpr float kVelocidadeParadoKmh = 3.0F;
constexpr std::uint32_t kSustentacaoParadoMs = 3000;

/// Decide se o veiculo esta parado ha tempo suficiente.
///
/// Serve as duas portas que o RF05.1 protege: o clique que abre o OTA e o
/// giro que abre o menu. Nos dois casos a razao e a mesma -- abrir em
/// movimento tira o alerta do motorista sem ele saber por quanto tempo.
///
/// **Sem fix nao ha velocidade, e ai sao dois casos diferentes:**
///
/// - Nunca houve fix desde o boot: conta como parado depois dos 3 s. E o
///   aparelho na bancada ou na garagem coberta, onde nao ha o que alertar
///   e onde justamente se quer mexer nos ajustes. Recusar aqui deixaria a
///   configuracao inalcancavel em todo lugar sem vista do ceu.
/// - Houve fix e ele se perdeu: **congela o veredito anterior**. Um carro
///   a 80 km/h que entra num tunel continua em movimento; deduzir que
///   parou porque o sinal sumiu seria inventar.
class DetectorParado {
public:
    void atualiza(float velocidade_kmh, std::uint32_t agora_ms);
    void sem_fix(std::uint32_t agora_ms);

    bool parado() const { return parado_; }

    /// Volta ao estado de boot. Para teste e para quando a base recarrega.
    void reinicia();

private:
    void conta(std::uint32_t agora_ms);

    bool teve_fix_ = false;
    bool parado_ = false;
    bool contando_ = false;
    std::uint32_t desde_ms_ = 0;
};

}  // namespace coruja
