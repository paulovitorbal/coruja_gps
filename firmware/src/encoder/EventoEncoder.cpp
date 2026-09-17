#include "encoder/EventoEncoder.h"

namespace coruja {

const char* nome_evento(EventoEncoder e) {
    switch (e) {
        case EventoEncoder::Nenhum:       return "nenhum";
        case EventoEncoder::GiroEsquerda: return "giro-esquerda";
        case EventoEncoder::GiroDireita:  return "giro-direita";
        case EventoEncoder::Clique:       return "clique";
    }
    return "?";
}

}  // namespace coruja
