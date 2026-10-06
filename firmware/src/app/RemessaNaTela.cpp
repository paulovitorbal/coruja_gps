#include "app/RemessaNaTela.h"

#include <cstdio>

namespace coruja {

FaseOta fase_equivalente(FaseRemessa f) {
    switch (f) {
        // Mesma coisa dos dois lados: subir o radio.
        case FaseRemessa::Conectando:  return FaseOta::Conectando;
        // Listar o cartao e perguntar o CRC sao esperas curtas sem barra --
        // o mesmo lugar que `Consultando` ocupa no OTA.
        case FaseRemessa::Listando:    return FaseOta::Consultando;
        case FaseRemessa::Confirmando: return FaseOta::Consultando;
        // A fase longa, com barra. No OTA os bytes descem; aqui sobem.
        case FaseRemessa::Enviando:    return FaseOta::Baixando;
        // Apagar e instantaneo, mas precisa de um nome na tela.
        case FaseRemessa::Apagando:    return FaseOta::Gravando;
        case FaseRemessa::Concluida:   return FaseOta::Concluida;
        case FaseRemessa::Falhou:      return FaseOta::Falhou;
    }
    return FaseOta::Falhou;  // fora de faixa: o lado seguro
}

void RemessaNaTela::fase(FaseRemessa f, const char* nome, unsigned indice,
                         unsigned total) {
    if (nome != nullptr && nome[0] != '\0' && total > 0) {
        std::snprintf(rotulo_, sizeof rotulo_, "%u/%u %s", indice, total, nome);
    } else {
        std::snprintf(rotulo_, sizeof rotulo_, "%s", descreve(f));
    }
    ponte_.define_rotulo(rotulo_);
    ponte_.fase(fase_equivalente(f), 1);
}

void RemessaNaTela::progresso(std::size_t enviados, std::size_t total) {
    ponte_.progresso(enviados, total);
}

void RemessaNaTela::falhou(const char* motivo) {
    // Limpa o rotulo do arquivo: a falha e da remessa, e deixar o nome do
    // ultimo arquivo por cima dela diria que o problema e naquele arquivo.
    rotulo_[0] = '\0';
    ponte_.define_rotulo(nullptr);
    ponte_.falhou(motivo);
}

}  // namespace coruja
