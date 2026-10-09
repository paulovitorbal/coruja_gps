#pragma once
#include <cstdint>

namespace coruja {

/// Quando desistir de um pedido de rede.
///
/// ## Por que não basta um prazo fixo
///
/// O prazo fixo não distingue **"a rede caiu"** de **"a rede está lenta"**, e
/// trata as duas do mesmo jeito. Em 09/10/2026 isso derrubou quatro tentativas
/// seguidas de enviar um `coruja.log` de 28 KB: o Wi-Fi estava a -88 dBm, o
/// enlace progredia o tempo todo — o aperto de mão TLS inteiro saiu, os três
/// certificados foram validados — e o pedido foi abortado no meio do corpo
/// porque trinta segundos haviam passado. Arquivos pequenos passavam na mesma
/// sessão, o que fazia o defeito parecer um limiar de tamanho.
///
/// Aqui o prazo é de **inatividade**: cada byte que entra ou sai renova. Um
/// enlace lento que avança não é punido por ser lento; um enlace travado
/// desiste depressa.
///
/// ## E por que ainda há um teto
///
/// Inatividade sozinha nunca desiste de quem rasteja: um enlace a dez bytes
/// por segundo renovaria o prazo para sempre, e o aparelho ficaria preso num
/// envio que não termina. O teto é a promessa de que o pedido acaba.
///
/// Puro de propósito: é a parte do `ClienteTls` que dá para exercitar no
/// host, inclusive na virada do relógio.
class PrazoDeProgresso {
public:
    PrazoDeProgresso(std::uint32_t inatividade_ms, std::uint32_t teto_ms,
                     std::uint32_t agora_ms)
        : inatividade_ms_(inatividade_ms), teto_ms_(teto_ms),
          inicio_ms_(agora_ms), ultimo_progresso_ms_(agora_ms) {}

    /// Algo andou: byte recebido, byte reconhecido, conexão estabelecida.
    void registra_progresso(std::uint32_t agora_ms) {
        ultimo_progresso_ms_ = agora_ms;
    }

    bool expirou(std::uint32_t agora_ms) const {
        return parado_ha(agora_ms) >= inatividade_ms_ ||
               decorrido(agora_ms) >= teto_ms_;
    }

    /// Qual dos dois venceu. Interessa ao log: "parado" manda olhar o sinal,
    /// "teto" manda olhar o tamanho do arquivo.
    bool estourou_o_teto(std::uint32_t agora_ms) const {
        return decorrido(agora_ms) >= teto_ms_;
    }

    /// Subtração sem sinal: correta na virada dos 32 bits, que chega a cada
    /// ~49 dias de aparelho ligado.
    std::uint32_t parado_ha(std::uint32_t agora_ms) const {
        return agora_ms - ultimo_progresso_ms_;
    }
    std::uint32_t decorrido(std::uint32_t agora_ms) const {
        return agora_ms - inicio_ms_;
    }

private:
    std::uint32_t inatividade_ms_;
    std::uint32_t teto_ms_;
    std::uint32_t inicio_ms_;
    std::uint32_t ultimo_progresso_ms_;
};

}  // namespace coruja
