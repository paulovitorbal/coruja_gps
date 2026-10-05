#include "PortesHost.h"

#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "log/Logger.h"

namespace coruja::host {

// --------------------------------------------------------------- UartPty

void UartPty::escreve(const std::uint8_t* b, std::size_t n) {
    // A prévia não configura o módulo: o simulador já emite na taxa certa.
    if (::write(fd_, b, n) < 0) { /* ignorado de propósito */ }
}

std::size_t UartPty::le(std::uint8_t* destino, std::size_t capacidade) {
    const ssize_t n = ::read(fd_, destino, capacidade);
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}

// ------------------------------------------------------ ArmazenamentoArquivo

std::string ArmazenamentoArquivo::caminho(const char* nome) const {
    return raiz_ + "/" + nome;
}

ErroCartao ArmazenamentoArquivo::le_arquivo(const char* nome, char* destino,
                                            std::size_t capacidade,
                                            std::size_t* lidos, Logger&) {
    std::FILE* f = std::fopen(caminho(nome).c_str(), "rb");
    if (f == nullptr) { return ErroCartao::ArquivoAusente; }
    std::fseek(f, 0, SEEK_END);
    const long tam = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (tam < 0 || static_cast<std::size_t>(tam) >= capacidade) {
        std::fclose(f);
        return ErroCartao::ArquivoGrande;
    }
    const bool ok =
        std::fread(destino, 1, static_cast<std::size_t>(tam), f) ==
        static_cast<std::size_t>(tam);
    std::fclose(f);
    if (!ok) { return ErroCartao::FalhaDeLeitura; }
    if (lidos != nullptr) { *lidos = static_cast<std::size_t>(tam); }
    return ErroCartao::Nenhum;
}

ErroCartao ArmazenamentoArquivo::grava_arquivo(const char* nome,
                                               const char* conteudo,
                                               std::size_t tamanho, Logger&) {
    std::FILE* f = std::fopen(caminho(nome).c_str(), "wb");
    if (f == nullptr) { return ErroCartao::FalhaDeEscrita; }
    const bool ok = std::fwrite(conteudo, 1, tamanho, f) == tamanho;
    std::fclose(f);
    ++escritas_;
    return ok ? ErroCartao::Nenhum : ErroCartao::FalhaDeEscrita;
}

ErroCartao ArmazenamentoArquivo::acrescenta_arquivo(const char* nome,
                                                    const char* conteudo,
                                                    std::size_t tamanho,
                                                    Logger&) {
    std::FILE* f = std::fopen(caminho(nome).c_str(), "ab");
    if (f == nullptr) { return ErroCartao::FalhaDeEscrita; }
    const bool ok = std::fwrite(conteudo, 1, tamanho, f) == tamanho;
    std::fclose(f);
    ++escritas_;
    return ok ? ErroCartao::Nenhum : ErroCartao::FalhaDeEscrita;
}

ErroCartao ArmazenamentoArquivo::abre_para_escrita(const char* nome, Logger&) {
    fluxo_ = std::fopen(caminho(nome).c_str(), "wb");
    nome_fluxo_ = nome;
    return fluxo_ != nullptr ? ErroCartao::Nenhum : ErroCartao::FalhaDeEscrita;
}

bool ArmazenamentoArquivo::escreve(const std::uint8_t* bytes,
                                   std::size_t tamanho) {
    if (fluxo_ == nullptr) { return false; }
    return std::fwrite(bytes, 1, tamanho, fluxo_) == tamanho;
}

ErroCartao ArmazenamentoArquivo::conclui_escrita(Logger&) {
    if (fluxo_ == nullptr) { return ErroCartao::FalhaDeEscrita; }
    std::fclose(fluxo_);
    fluxo_ = nullptr;
    ++escritas_;
    return ErroCartao::Nenhum;
}

void ArmazenamentoArquivo::descarta_escrita(const char* nome, Logger&) {
    if (fluxo_ != nullptr) {
        std::fclose(fluxo_);
        fluxo_ = nullptr;
    }
    std::remove(caminho(nome).c_str());
}

ErroCartao ArmazenamentoArquivo::promove(const char* temporario,
                                         const char* base,
                                         const char* reserva, Logger&) {
    std::remove(caminho(reserva).c_str());
    std::rename(caminho(base).c_str(), caminho(reserva).c_str());
    if (std::rename(caminho(temporario).c_str(), caminho(base).c_str()) != 0) {
        return ErroCartao::FalhaDeRenomeacao;
    }
    ++escritas_;
    return ErroCartao::Nenhum;
}

// ------------------------------------------------------- EncoderTeclado

EncoderTeclado::EncoderTeclado() {
    termios t{};
    if (::tcgetattr(STDIN_FILENO, &t) != 0) { return; }
    t.c_lflag = static_cast<tcflag_t>(t.c_lflag & ~(ICANON | ECHO));
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    cru_ = ::tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0;
}

EncoderTeclado::~EncoderTeclado() {
    if (!cru_) { return; }
    termios t{};
    if (::tcgetattr(STDIN_FILENO, &t) == 0) {
        t.c_lflag = static_cast<tcflag_t>(t.c_lflag | ICANON | ECHO);
        ::tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
}

EventoEncoder EncoderTeclado::proximo_evento() {
    char c = 0;
    if (::read(STDIN_FILENO, &c, 1) != 1) { return EventoEncoder::Nenhum; }
    switch (c) {
        case 'a': case 'A': return EventoEncoder::GiroEsquerda;
        case 'd': case 'D': return EventoEncoder::GiroDireita;
        case ' ': case '\n': return EventoEncoder::Clique;
        default: return EventoEncoder::Nenhum;
    }
}

// ----------------------------------------------------------- LedTexto

std::string LedTexto::nome() const {
    if (atual_ == cores::kApagado)  { return "apagado"; }
    if (atual_ == cores::kVerde)    { return "VERDE"; }
    if (atual_ == cores::kAmarelo)  { return "AMARELO"; }
    if (atual_ == cores::kRosa)     { return "ROSA"; }
    if (atual_ == cores::kVermelho) { return "VERMELHO"; }
    return "?";
}

}  // namespace coruja::host
