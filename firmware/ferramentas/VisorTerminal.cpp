#include "VisorTerminal.h"

#include <cstdio>
#include <vector>

namespace coruja {
namespace {

/// RGB565 de volta a 24 bits, para o ANSI de cor verdadeira. A ida e a volta
/// perdem os bits baixos, e é isso mesmo: o que se vê aqui é o que o painel
/// consegue mostrar, não o que o código pretendia.
std::uint32_t para_rgb888(Cor565 c) {
    const std::uint32_t r = ((c >> 11) & 0x1F) * 255 / 31;
    const std::uint32_t g = ((c >> 5) & 0x3F) * 255 / 63;
    const std::uint32_t b = (c & 0x1F) * 255 / 31;
    return (r << 16) | (g << 8) | b;
}

/// Fonte de bloco 3×5 para o número. O painel usa 56×94 e aqui cabem cinco
/// linhas — a proporção não é a mesma, mas o **lugar** e o tamanho relativo
/// na tela são, que é o que se quer conferir.
const char* glifo_de(char c, int linha) {
    static const char* kDigitos[13][5] = {
        {"###", "# #", "# #", "# #", "###"},  // 0
        {" # ", "## ", " # ", " # ", "###"},  // 1
        {"###", "  #", "###", "#  ", "###"},  // 2
        {"###", "  #", "###", "  #", "###"},  // 3
        {"# #", "# #", "###", "  #", "  #"},  // 4
        {"###", "#  ", "###", "  #", "###"},  // 5
        {"###", "#  ", "###", "# #", "###"},  // 6
        {"###", "  #", "  #", "  #", "  #"},  // 7
        {"###", "# #", "###", "# #", "###"},  // 8
        {"###", "# #", "###", "  #", "###"},  // 9
        {"  #", "  #", " # ", "#  ", "#  "},  // /
        {"   ", "   ", "###", "   ", "   "},  // -
        {"   ", "   ", "   ", "   ", "   "},  // espaço
    };
    int i = 12;
    if (c >= '0' && c <= '9') { i = c - '0'; }
    else if (c == '/') { i = 10; }
    else if (c == '-') { i = 11; }
    return kDigitos[i][linha];
}

/// Emoji, porque aqui **há** pilha de fontes. No painel são sprites
/// próprios, e o §4.1 explica por quê: não há sistema operacional no Pico e
/// o emoji colorido do desktop vem de uma fonte de vários megabytes.
///
const char* nome_do_icone(Icone i) {
    switch (i) {
        case Icone::Radar:            return "\U0001F3CE";              // 🏎
        case Icone::Semaforo:         return "\U0001F6A6";              // 🚦
        case Icone::SemaforoComRadar: return "\U0001F6A6\U0001F3CE";   // 🚦+🏎
        case Icone::Nenhum:           break;
    }
    return "";
}

/// Quantas colunas o glifo ocupa. Emoji são largos; ASCII não.
int largura_de(const std::string& g) {
    return (!g.empty() && static_cast<unsigned char>(g[0]) >= 0xF0) ? 2 : 1;
}

/// Quebra uma cadeia UTF-8 em glifos, para o emoji não ser cortado no meio.
std::vector<std::string> glifos(const std::string& s) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        std::size_t n = 1;
        if (b >= 0xF0) { n = 4; } else if (b >= 0xE0) { n = 3; }
        else if (b >= 0xC0) { n = 2; }
        if (i + n > s.size()) { n = 1; }
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}

}  // namespace

VisorTerminal::VisorTerminal() : grade_(kColunas * kLinhas) {}

int VisorTerminal::coluna_de(int x) { return x * kColunas / tela::kLargura; }
int VisorTerminal::linha_de(int y) { return y * kLinhas / tela::kAltura; }

void VisorTerminal::escreve(int coluna, int linha, const std::string& g,
                            std::uint32_t cor, int largura) {
    if (coluna < 0 || coluna >= kColunas || linha < 0 || linha >= kLinhas) {
        return;
    }
    auto& cel = grade_[linha * kColunas + coluna];
    cel.glifo = g;
    cel.frente = cor;
    cel.continuacao = false;
    for (int k = 1; k < largura && coluna + k < kColunas; ++k) {
        auto& seguinte = grade_[linha * kColunas + coluna + k];
        seguinte.glifo.clear();
        seguinte.continuacao = true;
    }
}

void VisorTerminal::escreve_texto(int coluna, int linha, const std::string& s,
                                  std::uint32_t cor) {
    int c = coluna;
    for (const auto& g : glifos(s)) {
        const int w = largura_de(g);
        escreve(c, linha, g, cor, w);
        c += w;
    }
}

int VisorTerminal::largura_em_colunas(const std::string& s) {
    int n = 0;
    for (const auto& g : glifos(s)) { n += largura_de(g); }
    return n;
}

void VisorTerminal::numero_grande(int centro, int linha, const std::string& s,
                                  std::uint32_t cor) {
    const int largura = static_cast<int>(s.size()) * 4 - 1;
    const int x0 = centro - largura / 2;
    for (int l = 0; l < 5; ++l) {
        for (std::size_t i = 0; i < s.size(); ++i) {
            const char* g = glifo_de(s[i], l);
            for (int k = 0; k < 3; ++k) {
                if (g[k] != ' ') {
                    escreve(x0 + static_cast<int>(i) * 4 + k, linha + l, "#",
                            cor);
                }
            }
        }
    }
}

void VisorTerminal::retangulo(int x, int y, int largura, int altura,
                              Cor565 cor) {
    const std::uint32_t rgb = para_rgb888(cor);
    const int c0 = coluna_de(x);
    const int l0 = linha_de(y);
    int c1 = coluna_de(x + largura);
    int l1 = linha_de(y + altura);
    // A grade tem 10 px por linha e 4 por coluna. Um retângulo mais fino que
    // isso — a moldura de 2 px é o caso — sumiria por arredondamento, e a
    // ausência pareceria defeito de desenho em vez de limite da grade.
    if (l1 == l0 && altura > 0) { ++l1; }
    if (c1 == c0 && largura > 0) { ++c1; }
    for (int l = l0; l < l1 && l < kLinhas; ++l) {
        for (int c = c0; c < c1 && c < kColunas; ++c) {
            if (l < 0 || c < 0) { continue; }
            auto& cel = grade_[l * kColunas + c];
            cel.fundo = rgb;
            cel.glifo = ' ';
        }
    }
}

void VisorTerminal::texto(int x, int y, const char* texto, Fonte fonte,
                          Cor565 cor, Alinhamento alinhamento) {
    const std::uint32_t rgb = para_rgb888(cor);
    if (fonte == Fonte::Numero) {
        numero_grande(coluna_de(x), linha_de(y), texto, rgb);
        return;
    }
    int c = coluna_de(x);
    if (alinhamento == Alinhamento::Centro) {
        c -= largura_em_colunas(texto) / 2;
    }
    escreve_texto(c, linha_de(y), texto, rgb);
}

void VisorTerminal::icone(int x, int y, Icone icone) {
    escreve_texto(coluna_de(x), linha_de(y) + 1, nome_do_icone(icone),
                  0xFFFFFF);
}

void VisorTerminal::apresenta() {
    std::string saida;
    if (primeira_) {
        saida += "\033[2J";     // limpa uma vez
        primeira_ = false;
    }
    saida += "\033[H";          // e daqui em diante só reposiciona: sem
                                // rolagem, o painel fica parado na tela
    char cor[48];
    for (int l = 0; l < kLinhas; ++l) {
        std::uint32_t ultima_f = 0xFFFFFFFF;
        std::uint32_t ultimo_b = 0xFFFFFFFF;
        for (int c = 0; c < kColunas; ++c) {
            const auto& cel = grade_[l * kColunas + c];
            if (cel.frente != ultima_f || cel.fundo != ultimo_b) {
                std::snprintf(cor, sizeof cor,
                              "\033[38;2;%u;%u;%um\033[48;2;%u;%u;%um",
                              (cel.frente >> 16) & 0xFF,
                              (cel.frente >> 8) & 0xFF, cel.frente & 0xFF,
                              (cel.fundo >> 16) & 0xFF,
                              (cel.fundo >> 8) & 0xFF, cel.fundo & 0xFF);
                saida += cor;
                ultima_f = cel.frente;
                ultimo_b = cel.fundo;
            }
            if (!cel.continuacao) { saida += cel.glifo; }
        }
        saida += "\033[0m\n";
    }
    saida += "\033[0m" + rodape_ + "\033[K\n";
    std::fwrite(saida.data(), 1, saida.size(), stdout);
    std::fflush(stdout);
}

}  // namespace coruja
