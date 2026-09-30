#include "app/OtaNaTela.h"

namespace coruja {

void OtaNaTela::fase(FaseOta fase, unsigned tentativa) {
    // **Zera o progresso a cada aviso NOVO**, e o "novo" inclui trocar de
    // tentativa sem trocar de fase.
    //
    // Duas razões distintas, e a segunda me escapou na primeira versão:
    //
    // 1. Saindo do download, a barra da tentativa anterior ficaria cheia
    //    por baixo da fase nova, dizendo que o que falhou tinha terminado.
    // 2. Entre `Baixando 1` e `Baixando 2` a fase é a MESMA, e sem
    //    comparar a tentativa o progresso antigo sobrevive: a barra
    //    começaria a segunda tentativa em 60% e depois voltaria ao zero
    //    quando os primeiros bytes chegassem. Barra que anda para trás é
    //    pior que barra nenhuma.
    const bool aviso_novo = fase != estado_.fase ||
                            tentativa != estado_.tentativa;
    estado_.fase = fase;
    estado_.tentativa = tentativa;
    if (aviso_novo) {
        estado_.recebidos = 0;
        estado_.total = 0;
    }
    padrao_.define_fase(fase, relogio_.agora_ms());
    pinta();
}

void OtaNaTela::progresso(std::size_t recebidos, std::size_t total) {
    estado_.recebidos = recebidos;
    estado_.total = total;
    pinta();
}

void OtaNaTela::mantem() { pinta(); }

void OtaNaTela::pinta() {
    // A tela só redesenha o que mudou, então chamar isto a cada pedaço do
    // download é barato: a barra só vai ao painel quando o pixel dela muda.
    tela_.desenha(estado_, visor_);
    led_.define_cor(padrao_.cor(relogio_.agora_ms()));
}

}  // namespace coruja
