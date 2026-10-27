#include "nucleo/LinhaVigia.h"

#include <cstdio>

namespace coruja {

std::size_t formata_vigia(const EstadoVigia& estado, char* destino,
                          std::size_t capacidade) {
    if (destino == nullptr || capacidade == 0) { return 0; }
    destino[0] = '\0';

    // Os dois trechos opcionais saem para buffers próprios porque o que falta
    // medir precisa DIZER que não foi medido, e um `%.1f` de zero diria o
    // contrário com a mesma cara de número bom.
    char hdop[24] = "sem medida";
    if (estado.tem_qualidade) {
        std::snprintf(hdop, sizeof hdop, "%.1f (%u sat)",
                      static_cast<double>(estado.hdop),
                      static_cast<unsigned>(estado.satelites));
    }
    char temperatura[16] = "sem medida";
    if (estado.tem_temperatura) {
        std::snprintf(temperatura, sizeof temperatura, "%.1f",
                      static_cast<double>(estado.temperatura_c));
    }

    const int n = std::snprintf(
        destino, capacidade,
        "base %lu pts | fix %s | zona %u | hdop %s | temp %s | "
        "serial perdeu %lu B | pior volta %lu ms",
        static_cast<unsigned long>(estado.pontos_base),
        estado.tem_fix ? "sim" : "NAO",
        static_cast<unsigned>(estado.zona), hdop, temperatura,
        static_cast<unsigned long>(estado.serial_perdidos),
        static_cast<unsigned long>(estado.pior_volta_ms));

    // `snprintf` trunca e devolve o que TERIA escrito. Meia linha num diário
    // é pior que linha nenhuma: quem lê não sabe que faltou pedaço, e os
    // campos cortados são sempre os últimos — justamente os dois que a
    // instrumentação de 26/10/2026 acrescentou.
    if (n < 0 || static_cast<std::size_t>(n) >= capacidade) {
        destino[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(n);
}

}  // namespace coruja
