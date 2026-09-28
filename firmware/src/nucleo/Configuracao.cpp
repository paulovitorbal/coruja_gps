#include "nucleo/Configuracao.h"

namespace coruja {

const char* descreve(ModoNoturno m) {
    switch (m) {
        case ModoNoturno::Automatico:  return "auto";
        case ModoNoturno::SempreDia:   return "dia";
        case ModoNoturno::SempreNoite: return "noite";
    }
    return "?";
}

}  // namespace coruja
