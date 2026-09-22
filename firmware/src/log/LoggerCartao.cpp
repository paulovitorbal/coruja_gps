#include "log/LoggerCartao.h"

#include <cstdio>
#include <cstring>

#include "armazenamento/CartaoSd.h"

namespace coruja {

LoggerCartao::LoggerCartao(Logger& seguinte, CartaoSd& cartao,
                           const char* nome_arquivo)
    : seguinte_(seguinte), cartao_(cartao), nome_(nome_arquivo) {}

void LoggerCartao::define_nivel_minimo(Nivel nivel) {
    minimo_ = nivel;
    seguinte_.define_nivel_minimo(nivel);
}

void LoggerCartao::grava_no_cartao(bool ligado) {
    if (!ligado) {
        descarrega();
    }
    ligado_ = ligado;
}

void LoggerCartao::descarrega() {
    if (usados_ == 0 || !ligado_ || gravando_) {
        return;
    }
    // O guarda é o que impede a recursão: o `CartaoSd` registra as próprias
    // falhas, e sem isto uma falha de gravação tentaria gravar a mensagem
    // sobre a falha de gravação.
    gravando_ = true;
    // O logger passado é o SEGUINTE, não `*this`. Assim as mensagens do
    // cartão aparecem no console e não realimentam o buffer.
    const bool ok = cartao_.acrescenta_arquivo(nome_, buffer_, usados_,
                                               seguinte_) == ErroCartao::Nenhum;
    gravando_ = false;

    if (!ok) {
        // ⚠️ **Mantém o conteúdo.** A primeira versão zerava o buffer antes de
        // tentar gravar, para ele não ficar preso — e trocou "preso" por
        // "perdido em silêncio". Durante o download da base a gravação falha
        // sempre, de propósito, e cada tentativa jogava fora 1 KiB de log,
        // inclusive linhas de ANTES do download. Medido: 14 linhas sumiram de
        // uma execução real.
        return;
    }
    usados_ = 0;

    // Se linhas foram descartadas por buffer cheio, o arquivo tem de dizer.
    // Log com buraco silencioso é pior que log curto: ele faz quem lê concluir
    // que o evento não aconteceu.
    if (perdidas_ > 0) {
        char aviso[96];
        const int n = std::snprintf(aviso, sizeof aviso,
                                    "[AVISO] log: %u linha(s) perdida(s) por "
                                    "buffer cheio\n",
                                    static_cast<unsigned>(perdidas_));
        if (n > 0) {
            gravando_ = true;
            cartao_.acrescenta_arquivo(nome_, aviso,
                                       static_cast<std::size_t>(n), seguinte_);
            gravando_ = false;
        }
        perdidas_ = 0;
    }
}

void LoggerCartao::registra(Nivel nivel, const char* origem,
                            const char* mensagem) {
    seguinte_.registra(nivel, origem, mensagem);

    if (!ligado_ || gravando_ || nivel < minimo_) {
        return;
    }

    char linha[160];
    const int n = std::snprintf(linha, sizeof linha, "[%s] %s: %s\n",
                                nome_nivel(nivel), origem, mensagem);
    if (n <= 0) {
        return;
    }
    const std::size_t tam = static_cast<std::size_t>(n) < sizeof linha
                                ? static_cast<std::size_t>(n)
                                : sizeof linha - 1;

    if (usados_ + tam > kTamBuffer) {
        descarrega();
    }
    if (usados_ + tam <= kTamBuffer) {
        std::memcpy(buffer_ + usados_, linha, tam);
        usados_ += tam;
    } else {
        // Buffer cheio e a descarga não passou — tipicamente porque uma
        // escrita em fluxo está em curso. Conta, para o arquivo poder
        // declarar o buraco em vez de escondê-lo.
        ++perdidas_;
    }

    // Grave não espera: num travamento, o que se quer ler é a última linha.
    if (nivel >= Nivel::Warning) {
        descarrega();
    }
}

}  // namespace coruja
