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
        // **E o motivo junto.** Ele chega atrasado, pelo `falhou()`, e sem
        // isto sobreviveria a tentativa seguinte: entre o `fase(Falhou)` de
        // dentro do orquestrador e o `falhou()` da composicao, a tela
        // mostraria a causa da tentativa PASSADA. Motivo errado e pior que
        // motivo nenhum -- manda consertar o que nao esta quebrado.
        estado_.motivo = nullptr;
    }
    padrao_.define_fase(fase, relogio_.agora_ms());
    pinta();
}

void OtaNaTela::progresso(std::size_t recebidos, std::size_t total) {
    estado_.recebidos = recebidos;
    estado_.total = total;
    pinta();
}

void OtaNaTela::falhou(const char* motivo) {
    // Nao passa pelo `fase()` de proposito: la um aviso novo zera o
    // progresso, e aqui nao ha progresso novo nenhum -- so o nome da causa
    // chegando atrasado para uma falha ja anunciada.
    estado_.fase = FaseOta::Falhou;
    estado_.motivo = motivo;
    padrao_.define_fase(FaseOta::Falhou, relogio_.agora_ms());
    pinta();
}

void OtaNaTela::mantem() { pinta(); }

void OtaNaTela::pinta() {
    // A tela só redesenha o que mudou, e o progresso dela anda de 5 em 5 por
    // cento — então chamar isto a cada pedaço do download é barato. Vale
    // chamar de qualquer forma: o LED pisca por tempo, não por evento.
    tela_.desenha(estado_, visor_);
    led_.define_cor(padrao_.cor(relogio_.agora_ms()));
}

}  // namespace coruja
