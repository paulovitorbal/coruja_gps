#include "display/DesenhaTexto.h"

#include "display/FonteNumero.h"
#include "display/FonteNumeroPequeno.h"
#include "display/FonteTexto.h"
#include "display/FonteTextoGrande.h"

namespace coruja {

int largura_numero(const char* texto) {
    int n = 0;
    for (const char* p = texto; *p != '\0'; ++p) {
        if (fonte::numero::indice(*p) >= 0) {
            n += fonte::numero::kLargura;
        }
    }
    return n;
}

int largura_texto(const char* texto) {
    int n = 0;
    for (const char* p = texto; *p != '\0'; ++p) {
        if (fonte::texto::indice(*p) >= 0) {
            n += fonte::texto::kLargura;
        }
    }
    return n;
}

int escreve_numero(PainelSt7789& painel, int x, int y, const char* texto,
                   Cor565 cor, Cor565 fundo) {
    const int inicio = x;
    for (const char* p = texto; *p != '\0'; ++p) {
        const int i = fonte::numero::indice(*p);
        if (i < 0) {
            continue;  // glifo ausente e ignorado: a fonte tem 11, nao 95
        }
        painel.desenha_bitmap(x, y, fonte::numero::kLargura,
                              fonte::numero::kAltura,
                              fonte::numero::kBytesPorLinha,
                              fonte::numero::kBitmap[i], cor, fundo);
        x += fonte::numero::kLargura;
    }
    return x - inicio;
}

int escreve_texto(PainelSt7789& painel, int x, int y, const char* texto,
                  Cor565 cor, Cor565 fundo) {
    const int inicio = x;
    for (const char* p = texto; *p != '\0'; ++p) {
        const int i = fonte::texto::indice(*p);
        if (i < 0) {
            continue;
        }
        painel.desenha_bitmap(x, y, fonte::texto::kLargura,
                              fonte::texto::kAltura,
                              fonte::texto::kBytesPorLinha,
                              fonte::texto::kBitmap[i], cor, fundo);
        x += fonte::texto::kLargura;
    }
    return x - inicio;
}

int escreve_texto_grande(PainelSt7789& painel, int x, int y, const char* texto,
                  Cor565 cor, Cor565 fundo) {
    const int inicio = x;
    for (const char* p = texto; *p != '\0'; ++p) {
        const int i = fonte::textogrande::indice(*p);
        if (i < 0) {
            continue;
        }
        painel.desenha_bitmap(x, y, fonte::textogrande::kLargura,
                              fonte::textogrande::kAltura,
                              fonte::textogrande::kBytesPorLinha,
                              fonte::textogrande::kBitmap[i], cor, fundo);
        x += fonte::textogrande::kLargura;
    }
    return x - inicio;
}

int largura_numero_pequeno(const char* texto) {
    int n = 0;
    for (const char* p = texto; *p != '\0'; ++p) {
        if (fonte::numeropequeno::indice(*p) >= 0) {
            n += fonte::numeropequeno::kLargura;
        }
    }
    return n;
}

int escreve_numero_pequeno(PainelSt7789& painel, int x, int y,
                           const char* texto, Cor565 cor, Cor565 fundo) {
    const int inicio = x;
    for (const char* p = texto; *p != '\0'; ++p) {
        const int i = fonte::numeropequeno::indice(*p);
        if (i < 0) {
            continue;
        }
        painel.desenha_bitmap(x, y, fonte::numeropequeno::kLargura,
                              fonte::numeropequeno::kAltura,
                              fonte::numeropequeno::kBytesPorLinha,
                              fonte::numeropequeno::kBitmap[i], cor, fundo);
        x += fonte::numeropequeno::kLargura;
    }
    return x - inicio;
}

}  // namespace coruja
