#include "app/ModoTesteEncoder.h"

namespace coruja {

Cor ModoTesteEncoder::aplica(EventoEncoder evento) {
    switch (evento) {
        case EventoEncoder::GiroEsquerda:
            cor_ = cores::kVermelho;
            break;
        case EventoEncoder::GiroDireita:
            cor_ = cores::kAzul;
            break;
        case EventoEncoder::Clique:
            cor_ = cores::kApagado;
            break;
        case EventoEncoder::Nenhum:
            break;
    }
    return cor_;
}

}  // namespace coruja
