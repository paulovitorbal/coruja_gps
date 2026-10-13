#include "nucleo/FaixaHdop.h"

namespace coruja {

FaixaHdop classifica_hdop(float hdop) {
    if (hdop <= 0.0F) { return FaixaHdop::Desconhecida; }
    if (hdop < 1.0F)  { return FaixaHdop::Ideal; }
    if (hdop < 2.0F)  { return FaixaHdop::Excelente; }
    if (hdop < 5.0F)  { return FaixaHdop::Bom; }
    if (hdop < 10.0F) { return FaixaHdop::Moderado; }
    return FaixaHdop::Ruim;
}

int barras_de_hdop(FaixaHdop faixa) {
    switch (faixa) {
        case FaixaHdop::Ideal:     return kBarrasHdop;
        case FaixaHdop::Excelente: return kBarrasHdop - 1;
        case FaixaHdop::Bom:       return kBarrasHdop - 2;
        case FaixaHdop::Moderado:  return kBarrasHdop - 3;
        case FaixaHdop::Ruim:      return 0;
        case FaixaHdop::Desconhecida: return 0;
    }
    return 0;
}

const char* descreve(FaixaHdop faixa) {
    switch (faixa) {
        case FaixaHdop::Desconhecida: return "sem medida";
        case FaixaHdop::Ideal:        return "ideal";
        case FaixaHdop::Excelente:    return "excelente";
        case FaixaHdop::Bom:          return "bom";
        case FaixaHdop::Moderado:     return "moderado";
        case FaixaHdop::Ruim:         return "ruim";
    }
    return "?";
}

}  // namespace coruja
