#include "nucleo/EscritorConfig.h"

#include <cstring>

#include "nucleo/LinhaConfig.h"

namespace coruja {

namespace {

/// Acumula em `destino` sem nunca passar de `capacidade`. Uma vez
/// estourado, continua contando e para de escrever: quem chama descobre
/// pelo total, e nao por um buraco no meio do arquivo.
class Saida {
public:
    Saida(char* destino, std::size_t capacidade)
        : destino_(destino), capacidade_(capacidade) {}

    void escreve(const char* bytes, std::size_t n) {
        if (escritos_ + n <= capacidade_) {
            std::memcpy(destino_ + escritos_, bytes, n);
        } else {
            estourou_ = true;
        }
        escritos_ += n;
    }

    void escreve(const char* texto) { escreve(texto, std::strlen(texto)); }

    void numero(unsigned v) {
        char buf[4];
        std::size_t n = 0;
        do {
            buf[n++] = static_cast<char>('0' + (v % 10U));
            v /= 10U;
        } while (v > 0 && n < sizeof(buf));
        while (n > 0) {
            --n;
            escreve(buf + n, 1);
        }
    }

    bool estourou() const { return estourou_; }
    std::size_t escritos() const { return escritos_; }

private:
    char* destino_;
    std::size_t capacidade_;
    std::size_t escritos_ = 0;
    bool estourou_ = false;
};

enum class Chave { Nome, BrilhoDia, BrilhoNoite, ModoNoturno, VolumeBuzzer,
                   Nenhuma };

constexpr const char* kNomes[] = {"nome", "brilho_dia", "brilho_noite",
                                  "modo_noturno", "volume_buzzer"};
constexpr std::size_t kQuantas = 5;

Chave reconhece(const char* chave, std::size_t n) {
    for (std::size_t i = 0; i < kQuantas; ++i) {
        if (igual(chave, n, kNomes[i])) {
            return static_cast<Chave>(i);
        }
    }
    return Chave::Nenhuma;
}

void valor_de(Saida& s, Chave c, const Configuracao& cfg) {
    switch (c) {
        case Chave::Nome:         s.escreve(cfg.nome); break;
        case Chave::BrilhoDia:    s.numero(cfg.brilho_dia); break;
        case Chave::BrilhoNoite:  s.numero(cfg.brilho_noite); break;
        case Chave::ModoNoturno:  s.escreve(descreve(cfg.modo_noturno)); break;
        case Chave::VolumeBuzzer: s.numero(cfg.volume_buzzer); break;
        case Chave::Nenhuma:      break;
    }
}

}  // namespace

std::size_t reescreve_ajustes(const char* origem, std::size_t tamanho,
                              const Configuracao& cfg, char* destino,
                              std::size_t capacidade) {
    if (destino == nullptr) {
        return 0;
    }
    if (origem == nullptr) {
        tamanho = 0;
    }

    Saida s(destino, capacidade);
    bool vista[kQuantas] = {};

    std::size_t i = 0;
    while (i < tamanho) {
        std::size_t fim = i;
        while (fim < tamanho && origem[fim] != '\n') {
            ++fim;
        }
        const bool tem_nova_linha = fim < tamanho;
        const char* linha = origem + i;
        const std::size_t n = fim - i;

        const ParChaveValor par = divide(linha, n);
        const Chave c = par.valida ? reconhece(par.chave, par.n_chave)
                                   : Chave::Nenhuma;
        if (c == Chave::Nenhuma) {
            s.escreve(linha, n);
        } else {
            vista[static_cast<std::size_t>(c)] = true;
            // Tudo ate o '=' fica como estava: indentacao e espacos em
            // volta do sinal sao de quem editou o arquivo, nao nossos.
            const std::size_t ate_igual =
                static_cast<std::size_t>(par.valor - linha);
            s.escreve(linha, ate_igual);
            valor_de(s, c, cfg);
            // Preserva o fim de linha do Windows, que estaria depois do
            // valor. Sem isto o arquivo sairia com finais misturados.
            if (n > 0 && linha[n - 1] == '\r') {
                s.escreve("\r", 1);
            }
        }

        if (tem_nova_linha) {
            s.escreve("\n", 1);
        }
        i = fim + 1;
    }

    // Chaves que o arquivo nao tinha. Vao para o fim, com um cabecalho,
    // porque um bloco sem explicacao num arquivo comentado assim parece
    // sujeira de programa.
    bool alguma_faltando = false;
    for (std::size_t k = 0; k < kQuantas; ++k) {
        alguma_faltando = alguma_faltando || !vista[k];
    }
    if (alguma_faltando) {
        // O '\n' inicial do cabecalho nao e enfeite: ele tambem fecha a
        // ultima linha quando o arquivo nao terminava em nova linha, que
        // senao grudaria o cabecalho no fim dela.
        s.escreve("\n# --- ajustes do aparelho (escritos pelo menu) ---\n");
        for (std::size_t k = 0; k < kQuantas; ++k) {
            if (vista[k]) {
                continue;
            }
            s.escreve(kNomes[k]);
            s.escreve("=", 1);
            valor_de(s, static_cast<Chave>(k), cfg);
            s.escreve("\n", 1);
        }
    }

    return s.estourou() ? 0 : s.escritos();
}

}  // namespace coruja
