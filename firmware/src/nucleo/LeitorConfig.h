#pragma once
#include <cstddef>

#include "nucleo/Configuracao.h"

namespace coruja {

class Logger;

/// O que o leitor encontrou de anormal. Nenhum destes impede o firmware de
/// operar — o ADR 0002 é explícito: configuração ausente ou ilegível assume
/// padrões e registra aviso, nunca deixa o aparelho de pé sem funcionar.
struct DiagnosticoConfig {
    std::size_t linhas_lidas       = 0;
    std::size_t linhas_sem_igual   = 0;  ///< texto solto, nem chave nem comentário
    std::size_t chaves_desconhecidas = 0;
    std::size_t valores_longos     = 0;  ///< **rejeitados**, nunca truncados
    std::size_t indices_fora       = 0;  ///< `wifi_ssid_9` com teto de 5 redes
    std::size_t redes_incompletas  = 0;  ///< senha sem SSID correspondente

    bool limpo() const {
        return linhas_sem_igual == 0 && chaves_desconhecidas == 0 &&
               valores_longos == 0 && indices_fora == 0 &&
               redes_incompletas == 0;
    }
};

struct ResultadoConfig {
    Configuracao      config;
    DiagnosticoConfig diagnostico;
};

/// Lê o `coruja.cfg` a partir do texto já em memória.
///
/// Não abre arquivo: recebe os bytes. É a mesma separação do `carrega_base` e
/// pelo mesmo motivo — permite testar o parser inteiro no host, incluindo
/// cada forma de linha malformada, sem cartão e sem Pico.
///
/// Formato: `chave=valor`, uma por linha. Linhas em branco e as que começam
/// com `#` são ignoradas. A divisão é **no primeiro `=`**, porque senha de
/// Wi-Fi pode conter qualquer caractere, inclusive o separador.
///
/// Chaves reconhecidas:
/// - `wifi_ssid_N` e `wifi_senha_N`, com `N` de 1 a `kMaxRedes`
/// - `url_versao`, `url_base`
///
/// **Valor longo demais é rejeitado, nunca truncado.** Truncar uma URL ou uma
/// senha produz um valor que parece válido e falha em campo com sintoma
/// obscuro; rejeitar produz um aviso no boot.
///
/// > ⚠️ Este leitor manipula a senha do Wi-Fi. **Nada aqui, nem no chamador,
/// > deve registrar o valor de `senha` em log.** Há teste que verifica isso.
ResultadoConfig le_config(const char* texto, std::size_t tamanho,
                          Logger* logger = nullptr);

}  // namespace coruja
