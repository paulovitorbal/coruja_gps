#include "LeArquivo.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace coruja::host {
namespace {

/// Fecha o `FILE*` em qualquer caminho de saída, inclusive nos de erro.
class Fecha {
public:
    explicit Fecha(std::FILE* f) : f_(f) {}
    ~Fecha() { if (f_ != nullptr) { std::fclose(f_); } }
    Fecha(const Fecha&) = delete;
    Fecha& operator=(const Fecha&) = delete;

private:
    std::FILE* f_;
};

}  // namespace

bool le_arquivo_inteiro(const char* caminho, std::vector<std::uint8_t>* destino,
                        std::string* erro) {
    std::FILE* f = std::fopen(caminho, "rb");
    if (f == nullptr) {
        *erro = std::string(caminho) + ": " + std::strerror(errno);
        return false;
    }
    Fecha fecha(f);

    // Diretório: no macOS o `fopen` ACEITA, e só a leitura falha. Recusar
    // aqui dá o nome do problema em vez de "leitura incompleta".
    struct stat st {};
    if (::fstat(::fileno(f), &st) != 0) {
        *erro = std::string(caminho) + ": " + std::strerror(errno);
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        *erro = std::string(caminho) + ": nao e um arquivo comum";
        return false;
    }

    if (std::fseek(f, 0, SEEK_END) != 0) {
        *erro = std::string(caminho) + ": " + std::strerror(errno);
        return false;
    }
    const long tam = std::ftell(f);
    if (tam < 0) {
        *erro = std::string(caminho) + ": nao consegui medir o tamanho (" +
                std::strerror(errno) + ")";
        return false;
    }
    std::rewind(f);

    destino->assign(static_cast<std::size_t>(tam), 0);
    if (std::fread(destino->data(), 1, destino->size(), f) != destino->size()) {
        *erro = std::string(caminho) + ": leitura incompleta";
        destino->clear();
        return false;
    }
    return true;
}

}  // namespace coruja::host
