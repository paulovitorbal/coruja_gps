#include "rede/RespostaHttp.h"

#include <cstring>

namespace coruja {
namespace {

bool branco(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/// -1 se nao for digito hexadecimal.
int hex(char c) {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

}  // namespace

std::uint32_t status_da_resposta(const char* texto, std::size_t tamanho) {
    // "HTTP/1.1 201 ..." -- o codigo comeca depois do primeiro espaco.
    constexpr std::size_t kMinimo = 12;  // "HTTP/x.y NNN"
    if (texto == nullptr || tamanho < kMinimo) { return 0; }
    if (std::memcmp(texto, "HTTP/", 5) != 0) { return 0; }

    std::size_t i = 5;
    while (i < tamanho && texto[i] != ' ') { ++i; }
    while (i < tamanho && texto[i] == ' ') { ++i; }
    if (i + 3 > tamanho) { return 0; }

    std::uint32_t codigo = 0;
    for (std::size_t j = 0; j < 3; ++j) {
        const char c = texto[i + j];
        if (c < '0' || c > '9') { return 0; }
        codigo = codigo * 10 + static_cast<std::uint32_t>(c - '0');
    }
    return codigo;
}

const char* corpo_da_resposta(const char* texto, std::size_t tamanho,
                              std::size_t* tamanho_do_corpo) {
    if (texto == nullptr || tamanho_do_corpo == nullptr) { return nullptr; }
    for (std::size_t i = 0; i + 1 < tamanho; ++i) {
        if (texto[i] == '\n' && texto[i + 1] == '\n') {
            *tamanho_do_corpo = tamanho - (i + 2);
            return texto + i + 2;
        }
        if (i + 3 < tamanho && std::memcmp(texto + i, "\r\n\r\n", 4) == 0) {
            *tamanho_do_corpo = tamanho - (i + 4);
            return texto + i + 4;
        }
    }
    return nullptr;
}

bool le_crc_hex(const char* texto, std::size_t tamanho,
                std::uint32_t* destino) {
    if (texto == nullptr || destino == nullptr) { return false; }

    std::size_t i = 0;
    while (i < tamanho && branco(texto[i])) { ++i; }
    if (i + 8 > tamanho) { return false; }

    std::uint32_t valor = 0;
    for (std::size_t j = 0; j < 8; ++j) {
        const int d = hex(texto[i + j]);
        if (d < 0) { return false; }
        valor = (valor << 4) | static_cast<std::uint32_t>(d);
    }
    i += 8;

    // Depois dos oito digitos so pode vir branco. Um nono digito significa
    // que o servidor mandou outra coisa, e ler os oito primeiros dela daria
    // um numero que parece um CRC.
    while (i < tamanho) {
        if (!branco(texto[i])) { return false; }
        ++i;
    }
    *destino = valor;
    return true;
}


namespace {

/// Compara sem diferenciar maiuscula, do tamanho de `prefixo`.
bool comeca_com_ic(const char* texto, const char* prefixo) {
    for (; *prefixo != '\0'; ++texto, ++prefixo) {
        char a = *texto;
        char b = *prefixo;
        if (a >= 'A' && a <= 'Z') { a = static_cast<char>(a - 'A' + 'a'); }
        if (b >= 'A' && b <= 'Z') { b = static_cast<char>(b - 'A' + 'a'); }
        if (a != b) { return false; }
    }
    return true;
}

}  // namespace

void LeitorRespostaHttp::reinicia() {
    fase_ = Fase::Cabecalhos;
    n_cabecalhos_ = 0;
    cabecalhos_[0] = '\0';
    status_ = 0;
    content_length_ = -1;
    por_pedacos_ = false;
    recebidos_ = 0;
    falta_no_pedaco_ = 0;
    n_linha_pedaco_ = 0;
}

bool LeitorRespostaHttp::processa_cabecalhos(AoReceberCorpo ao_receber,
                                             void* contexto) {
    std::size_t n_corpo = 0;
    const char* corpo = corpo_da_resposta(cabecalhos_, n_cabecalhos_, &n_corpo);
    if (corpo == nullptr) { return true; }  // ainda nao chegaram inteiros

    status_ = status_da_resposta(cabecalhos_, n_cabecalhos_);
    if (status_ == 0) { return false; }

    // Varre linha a linha. Cabecalho nao diferencia maiuscula, e o
    // Cloudflare manda em minuscula enquanto o servidor de referencia manda
    // capitalizado -- comparar byte a byte funcionaria contra um e nao
    // contra o outro.
    for (std::size_t i = 0; i < n_cabecalhos_; ++i) {
        const bool inicio_de_linha =
            i == 0 || (i >= 1 && cabecalhos_[i - 1] == '\n');
        if (!inicio_de_linha) { continue; }
        const char* linha = cabecalhos_ + i;
        if (comeca_com_ic(linha, "content-length:")) {
            long v = 0;
            const char* p = linha + 15;
            while (*p == ' ') { ++p; }
            bool teve_digito = false;
            for (; *p >= '0' && *p <= '9'; ++p) {
                v = v * 10 + (*p - '0');
                teve_digito = true;
            }
            if (teve_digito) { content_length_ = v; }
        } else if (comeca_com_ic(linha, "transfer-encoding:")) {
            // Basta conter "chunked": o campo pode listar varias
            // codificacoes, e a de pedacos e sempre a ultima.
            for (const char* p = linha; *p != '\0' && *p != '\n'; ++p) {
                if (comeca_com_ic(p, "chunked")) { por_pedacos_ = true; break; }
            }
        }
    }

    if (por_pedacos_) {
        fase_ = Fase::PedacoTamanho;
        n_linha_pedaco_ = 0;
    } else if (content_length_ == 0) {
        fase_ = Fase::Fim;
    } else if (content_length_ > 0) {
        fase_ = Fase::CorpoContado;
    } else {
        fase_ = Fase::CorpoAteFechar;
    }

    // O que veio junto dos cabecalhos ja e corpo: reentra pela mesma porta,
    // em vez de duplicar a maquina de estado aqui dentro.
    if (n_corpo > 0) {
        char restante[kMaxCabecalhos];
        std::memcpy(restante, corpo, n_corpo);
        return alimenta(reinterpret_cast<const std::uint8_t*>(restante),
                        n_corpo, ao_receber, contexto);
    }
    return true;
}

bool LeitorRespostaHttp::alimenta(const std::uint8_t* bytes,
                                  std::size_t tamanho,
                                  AoReceberCorpo ao_receber, void* contexto) {
    if (bytes == nullptr) { return false; }

    std::size_t i = 0;
    while (i < tamanho) {
        switch (fase_) {
            case Fase::Cabecalhos: {
                const std::size_t livre = kMaxCabecalhos - 1 - n_cabecalhos_;
                if (livre == 0) { return false; }  // cabecalho sem fim
                const std::size_t n = (tamanho - i) < livre ? (tamanho - i)
                                                            : livre;
                std::memcpy(cabecalhos_ + n_cabecalhos_, bytes + i, n);
                n_cabecalhos_ += n;
                cabecalhos_[n_cabecalhos_] = '\0';
                i += n;
                if (!processa_cabecalhos(ao_receber, contexto)) {
                    return false;
                }
                // Se processou, o restante ja foi consumido la dentro.
                if (fase_ != Fase::Cabecalhos) { return true; }
                break;
            }

            case Fase::CorpoAteFechar: {
                const std::size_t n = tamanho - i;
                recebidos_ += n;
                if (ao_receber != nullptr) {
                    ao_receber(contexto, bytes + i, n);
                }
                i += n;
                break;
            }

            case Fase::CorpoContado: {
                const auto falta = static_cast<std::size_t>(content_length_)
                                   - recebidos_;
                const std::size_t n = (tamanho - i) < falta ? (tamanho - i)
                                                            : falta;
                recebidos_ += n;
                if (ao_receber != nullptr && n > 0) {
                    ao_receber(contexto, bytes + i, n);
                }
                i += n;
                if (recebidos_ >= static_cast<std::size_t>(content_length_)) {
                    fase_ = Fase::Fim;
                }
                break;
            }

            case Fase::PedacoTamanho: {
                const char c = static_cast<char>(bytes[i++]);
                if (c == '\n') {
                    linha_pedaco_[n_linha_pedaco_] = '\0';
                    // O tamanho pode vir com extensoes depois de ';'.
                    std::size_t valor = 0;
                    bool teve = false;
                    for (const char* p = linha_pedaco_; *p != '\0'; ++p) {
                        int d = -1;
                        if (*p >= '0' && *p <= '9') { d = *p - '0'; }
                        else if (*p >= 'a' && *p <= 'f') { d = *p - 'a' + 10; }
                        else if (*p >= 'A' && *p <= 'F') { d = *p - 'A' + 10; }
                        else { break; }
                        valor = valor * 16 + static_cast<std::size_t>(d);
                        teve = true;
                    }
                    if (!teve) { return false; }
                    n_linha_pedaco_ = 0;
                    if (valor == 0) {
                        // Pedaco de tamanho zero: o corpo acabou. O que vem
                        // depois sao reboques, que este cliente nao le.
                        fase_ = Fase::Fim;
                    } else {
                        falta_no_pedaco_ = valor;
                        fase_ = Fase::PedacoDados;
                    }
                } else if (c != '\r') {
                    if (n_linha_pedaco_ + 1 >= sizeof linha_pedaco_) {
                        return false;
                    }
                    linha_pedaco_[n_linha_pedaco_++] = c;
                }
                break;
            }

            case Fase::PedacoDados: {
                const std::size_t n = (tamanho - i) < falta_no_pedaco_
                                      ? (tamanho - i) : falta_no_pedaco_;
                recebidos_ += n;
                if (ao_receber != nullptr && n > 0) {
                    ao_receber(contexto, bytes + i, n);
                }
                i += n;
                falta_no_pedaco_ -= n;
                if (falta_no_pedaco_ == 0) { fase_ = Fase::PedacoFimDeLinha; }
                break;
            }

            case Fase::PedacoFimDeLinha: {
                const char c = static_cast<char>(bytes[i++]);
                if (c == '\n') { fase_ = Fase::PedacoTamanho; }
                break;
            }

            case Fase::Fim:
                // Reboques do ultimo pedaco, ou lixo depois do corpo. Nao e
                // erro, e nao e corpo: descarta.
                i = tamanho;
                break;
        }
    }
    return true;
}

}  // namespace coruja
