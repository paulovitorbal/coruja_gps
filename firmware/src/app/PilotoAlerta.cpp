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

    // Dia ou noite nao depende de haver base carregada -- so de haver
    // data e posicao. Preso ao ramo do alerta, um aparelho com a base
    // ausente ou corrompida ficaria no preset de dia a noite inteira.
    // O fix, sim, e exigido: sem data o calculo nao tem entrada, e o
    // ultimo valor conhecido e melhor palpite que 'Desconhecido' --
    // entrar num tunel nao amanhece.
    if (gps_.tem_fix(agora_ms)) {
        seletor_.atualiza(gps_.telemetria(), agora_ms);
    }

    // `tem_base()`, e não `base_ != nullptr`: a base vive num array global de
    // 24000 posições, cujo endereço nunca é nulo. Checar só o ponteiro deixava
    // passar uma base de ZERO pontos, o laço não varria nada, e o veredito
    // saía `Segura` — o aparelho afirmando "nenhum radar a menos de 300 m"
    // quando o que havia era ausência de base.
    if (gps_.tem_fix(agora_ms) && tem_base()) {
        veredito_ = maquina_.avalia(gps_.telemetria(), base_, quantos_,
                                    agora_ms);
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
