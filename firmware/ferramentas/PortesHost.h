#pragma once
#include <cstdint>
#include <string>

#include "app/Aplicacao.h"
#include "armazenamento/Armazenamento.h"
#include "buzzer/Buzzer.h"
#include "encoder/Encoder.h"
#include "gps/Uart.h"
#include "led/LedRgb.h"

namespace coruja::host {

/// Portes do host para rodar o produto inteiro fora da placa.
///
/// **O que muda é só a borda.** `Aplicacao`, `DiarioBordo`, `MaquinaZona` e
/// as telas são os mesmos objetos que vão para o RP2350 (ADR 0001): a UART
/// vira uma pty, o cartão vira o sistema de arquivos, o encoder vira o
/// teclado, e o LED e o buzzer viram texto.

/// A UART do host: a pty que o simulador cria.
class UartPty final : public Uart {
public:
    explicit UartPty(int fd) : fd_(fd) {}
    void escreve(const std::uint8_t* b, std::size_t n) override;
    std::size_t le(std::uint8_t* destino, std::size_t capacidade) override;
    void define_baud(std::uint32_t) override {}

private:
    int fd_;
};

/// O cartão vira um diretório.
///
/// **É o porte que faltava para a prévia valer algo.** Sem ele o
/// `DiarioBordo` não tinha onde escrever, e os dois arquivos de log nunca
/// haviam sido produzidos pelo caminho de código real — só por dublês em
/// memória, nos testes unitários.
class ArmazenamentoArquivo final : public Armazenamento {
public:
    explicit ArmazenamentoArquivo(std::string raiz) : raiz_(std::move(raiz)) {}

    ErroCartao le_arquivo(const char* nome, char* destino,
                          std::size_t capacidade, std::size_t* lidos,
                          Logger& log) override;
    ErroCartao grava_arquivo(const char* nome, const char* conteudo,
                             std::size_t tamanho, Logger& log) override;
    ErroCartao acrescenta_arquivo(const char* nome, const char* conteudo,
                                  std::size_t tamanho, Logger& log) override;
    ErroCartao abre_para_escrita(const char* nome, Logger& log) override;
    bool escreve(const std::uint8_t* bytes, std::size_t tamanho) override;
    ErroCartao conclui_escrita(Logger& log) override;
    void descarta_escrita(const char* nome, Logger& log) override;
    ErroCartao promove(const char* temporario, const char* base,
                       const char* reserva, Logger& log) override;

    /// Quantas escritas foram feitas, por arquivo — o número que diz se o
    /// desgaste de cartão da decisão "grava na hora" é o que se supôs.
    std::size_t escritas() const { return escritas_; }

private:
    std::string caminho(const char* nome) const;

    std::string raiz_;
    std::FILE*  fluxo_ = nullptr;
    std::string nome_fluxo_;
    std::size_t escritas_ = 0;
};

/// O encoder vira o teclado, em modo cru e sem bloquear.
///
/// `a` gira à esquerda, `d` gira à direita, espaço clica. São as teclas do
/// simulador, para a mão não trocar de convenção entre os dois terminais.
class EncoderTeclado final : public Encoder {
public:
    EncoderTeclado();
    ~EncoderTeclado() override;
    EventoEncoder proximo_evento() override;

private:
    bool cru_ = false;
};

class LedTexto final : public LedRgb {
public:
    void define_cor(const Cor& c) override { atual_ = c; }
    Cor cor_atual() const override { return atual_; }
    std::string nome() const;

private:
    Cor atual_ = cores::kApagado;
};

class BuzzerTexto final : public Buzzer {
public:
    void define(bool l) override { ligado_ = l; }
    bool ligado() const override { return ligado_; }

private:
    bool ligado_ = false;
};

/// As duas ações que a `Aplicacao` delega e que não cabem no host.
///
/// O OTA fala com Wi-Fi e o teste de alertas é de bancada. Na prévia os dois
/// só anunciam — mas anunciam, porque clique sem efeito nenhum parece
/// defeito.
class AcoesTexto final : public AcoesAplicacao {
public:
    void atualiza_base() override { ++ota_; }
    void testa_alertas() override { ++teste_; }
    void envia_dados() override { ++envio_; }
    unsigned ota() const { return ota_; }
    unsigned teste() const { return teste_; }
    unsigned envio() const { return envio_; }

private:
    unsigned ota_ = 0;
    unsigned teste_ = 0;
    unsigned envio_ = 0;
};

}  // namespace coruja::host
