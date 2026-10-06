#include "display/FonteNumero.h"
#include "display/FonteNumeroPequeno.h"
#include "display/FonteTexto.h"
#include "display/FonteTextoGrande.h"
#include "display/Visor.h"

namespace coruja {

/// **Portável de propósito, e não junto do `VisorSt7789`.**
///
/// As tabelas de glifos são `constexpr` puro — nada de hardware — e quem
/// precisa da altura é a `TelaPrincipal`, que decide layout e roda no host.
/// Definir isto ao lado do driver faria a suíte de host não linkar assim
/// que a primeira tela consultasse a métrica.
int altura_da_fonte(Fonte f) {
    switch (f) {
        case Fonte::Numero:        return fonte::numero::kAltura;
        case Fonte::NumeroPequeno: return fonte::numeropequeno::kAltura;
        case Fonte::Texto:         return fonte::texto::kAltura;
        case Fonte::TextoGrande:   return fonte::textogrande::kAltura;
    }
    return 0;
}

/// Largura que o texto ocupará, sem desenhar.
///
/// Existe pela mesma razão: centralizar e quebrar linha são decisões de
/// layout, e layout é testado no host.
int largura_da_fonte(Fonte f, const char* texto) {
    if (texto == nullptr) { return 0; }
    int n = 0;
    for (const char* p = texto; *p != '\0'; ++p) {
        switch (f) {
            case Fonte::Numero:
                if (fonte::numero::indice(*p) >= 0) {
                    n += fonte::numero::kLargura;
                }
                break;
            case Fonte::NumeroPequeno:
                if (fonte::numeropequeno::indice(*p) >= 0) {
                    n += fonte::numeropequeno::kLargura;
                }
                break;
            case Fonte::Texto:
                if (fonte::texto::indice(*p) >= 0) {
                    n += fonte::texto::kLargura;
                }
                break;
            case Fonte::TextoGrande:
                if (fonte::textogrande::indice(*p) >= 0) {
                    n += fonte::textogrande::kLargura;
                }
                break;
        }
    }
    return n;
}

}  // namespace coruja
