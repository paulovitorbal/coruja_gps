#include "app/CicloCores.h"

namespace coruja {

Cor cor_do_estado(EstadoVia estado) {
    switch (estado) {
        case EstadoVia::Segura: return calibracao::kSegura;
        case EstadoVia::Ambar:  return calibracao::kAmbar;
        case EstadoVia::Rosa:   return calibracao::kRosa;
        case EstadoVia::Perigo: return calibracao::kPerigo;
    }
    return cores::kApagado;
}

const char* nome_estado(EstadoVia estado) {
    switch (estado) {
        case EstadoVia::Segura: return "segura (verde)";
        case EstadoVia::Ambar:  return "ambar";
        case EstadoVia::Rosa:   return "rosa (margem)";
        case EstadoVia::Perigo: return "perigo (vermelho)";
    }
    return "?";
}

EstadoVia CicloCores::aplica(EventoEncoder evento) {
    const auto indice = static_cast<std::size_t>(estado_);
    na_ponta_ = false;

    if (evento == EventoEncoder::GiroDireita) {
        if (indice + 1 >= kQuantosEstados) {
            na_ponta_ = true;
        } else {
            estado_ = static_cast<EstadoVia>(indice + 1);
        }
    } else if (evento == EventoEncoder::GiroEsquerda) {
        if (indice == 0) {
            na_ponta_ = true;
        } else {
            estado_ = static_cast<EstadoVia>(indice - 1);
        }
    }
    return estado_;
}

}  // namespace coruja
