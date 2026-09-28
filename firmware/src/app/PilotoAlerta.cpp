#include "app/PilotoAlerta.h"

namespace coruja {

void PilotoAlerta::define_base(const Ponto* base, std::size_t quantos) {
    base_ = base;
    quantos_ = quantos;
    // O alvo vigente é guardado por índice, e a base nova tem outros. Sem
    // isto, um radar poderia herdar a retenção de 340 m de outro.
    maquina_.reinicia();
}

void PilotoAlerta::passo(std::uint32_t agora_ms) {
    gps_.atualiza(agora_ms);

    if (gps_.tem_fix(agora_ms) && base_ != nullptr) {
        veredito_ = maquina_.avalia(gps_.telemetria(), base_, quantos_,
                                    agora_ms);
        // O periodo so avanca com fix: sem data e sem posicao o calculo
        // nao tem entrada, e o ultimo valor conhecido e melhor palpite
        // que 'Desconhecido' -- entrar num tunel nao amanhece.
        seletor_.atualiza(gps_.telemetria(), agora_ms);
    } else {
        // Sem fix **ou sem base**: nos dois casos não há o que afirmar sobre
        // a via. Tratá-los igual é deliberado — o motorista não precisa
        // saber qual dos dois faltou para entender que não há alerta.
        veredito_ = maquina_.sem_fix(agora_ms);
    }

    padrao_.define_zona(veredito_.zona, agora_ms);
    led_.define_cor(padrao_.cor(agora_ms));

    cadencia_.define_faixa(veredito_.faixa, agora_ms);
    buzzer_.define(cadencia_.atualiza(agora_ms));
}

}  // namespace coruja
