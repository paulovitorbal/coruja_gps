#include "nucleo/LeitorConfig.h"

#include <cstdio>
#include <cstring>

#include "log/Logger.h"

namespace coruja {

namespace {

constexpr const char* kOrigem = "config";

bool e_espaco(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/// Recorta espaços das duas pontas, ajustando ponteiro e tamanho no lugar.
void apara(const char*& ini, std::size_t& n) {
    while (n > 0 && e_espaco(*ini)) {
        ++ini;
        --n;
    }
    while (n > 0 && e_espaco(ini[n - 1])) {
        --n;
    }
}

bool igual(const char* ini, std::size_t n, const char* literal) {
    return std::strlen(literal) == n && std::strncmp(ini, literal, n) == 0;
}

/// Reconhece `prefixo` seguido de um dígito de 1 a 9. Devolve o índice
/// base-zero, ou -1 se não casar.
int indice_de(const char* ini, std::size_t n, const char* prefixo) {
    const std::size_t p = std::strlen(prefixo);
    if (n != p + 1 || std::strncmp(ini, prefixo, p) != 0) {
        return -1;
    }
    const char d = ini[p];
    if (d < '1' || d > '9') {
        return -1;
    }
    return d - '1';
}

/// Copia se couber. Devolve falso quando não cabe — e aí nada é copiado, de
/// propósito: meio valor é pior que valor nenhum.
bool copia(char* destino, std::size_t capacidade, const char* ini,
           std::size_t n) {
    if (n > capacidade) {
        return false;
    }
    std::memcpy(destino, ini, n);
    destino[n] = '\0';
    return true;
}

void avisa(Logger* logger, const char* msg) {
    if (logger != nullptr) {
        logger->warning(kOrigem, msg);
    }
}

}  // namespace

ResultadoConfig le_config(const char* texto, std::size_t tamanho,
                          Logger* logger) {
    ResultadoConfig r;
    if (texto == nullptr || tamanho == 0) {
        avisa(logger, "configuracao vazia ou ausente; OTA indisponivel");
        return r;
    }

    // Marca quais índices apareceram, para detectar senha sem SSID e para
    // compactar a lista no fim.
    bool tem_ssid[kMaxRedes] = {};
    bool tem_senha[kMaxRedes] = {};
    Rede brutas[kMaxRedes];

    std::size_t i = 0;
    while (i < tamanho) {
        std::size_t fim = i;
        while (fim < tamanho && texto[fim] != '\n') {
            ++fim;
        }

        const char* linha = texto + i;
        std::size_t n = fim - i;
        i = fim + 1;

        apara(linha, n);
        if (n == 0 || linha[0] == '#') {
            continue;
        }
        ++r.diagnostico.linhas_lidas;

        // Divide no PRIMEIRO '=': senha de Wi-Fi pode conter '='.
        std::size_t pos = 0;
        while (pos < n && linha[pos] != '=') {
            ++pos;
        }
        if (pos == n) {
            ++r.diagnostico.linhas_sem_igual;
            continue;
        }

        const char* chave = linha;
        std::size_t nc = pos;
        const char* valor = linha + pos + 1;
        std::size_t nv = n - pos - 1;
        apara(chave, nc);
        apara(valor, nv);

        int idx = indice_de(chave, nc, "wifi_ssid_");
        if (idx >= 0) {
            if (static_cast<std::size_t>(idx) >= kMaxRedes) {
                ++r.diagnostico.indices_fora;
            } else if (!copia(brutas[idx].ssid, kMaxSsid, valor, nv)) {
                ++r.diagnostico.valores_longos;
            } else {
                tem_ssid[idx] = true;
            }
            continue;
        }

        idx = indice_de(chave, nc, "wifi_senha_");
        if (idx >= 0) {
            if (static_cast<std::size_t>(idx) >= kMaxRedes) {
                ++r.diagnostico.indices_fora;
            } else if (!copia(brutas[idx].senha, kMaxSenha, valor, nv)) {
                // Nunca imprimir o valor: é a senha.
                ++r.diagnostico.valores_longos;
            } else {
                tem_senha[idx] = true;
            }
            continue;
        }

        if (igual(chave, nc, "url_versao")) {
            if (!copia(r.config.url_versao, kMaxUrl, valor, nv)) {
                ++r.diagnostico.valores_longos;
            }
            continue;
        }
        if (igual(chave, nc, "url_base")) {
            if (!copia(r.config.url_base, kMaxUrl, valor, nv)) {
                ++r.diagnostico.valores_longos;
            }
            continue;
        }

        ++r.diagnostico.chaves_desconhecidas;
    }

    // Compacta preservando a ordem do arquivo, que é a ordem de prioridade.
    // Índices salteados (1 e 3, sem o 2) são aceitos: recusá-los puniria um
    // erro de digitação que não tem consequência nenhuma.
    for (std::size_t k = 0; k < kMaxRedes; ++k) {
        if (!tem_ssid[k]) {
            if (tem_senha[k]) {
                ++r.diagnostico.redes_incompletas;
            }
            continue;
        }
        r.config.redes[r.config.n_redes++] = brutas[k];
    }

    if (logger != nullptr) {
        char msg[96];
        std::snprintf(msg, sizeof msg, "%u rede(s) configurada(s)",
                      static_cast<unsigned>(r.config.n_redes));
        logger->info(kOrigem, msg);
        if (!r.config.ota_possivel()) {
            logger->warning(kOrigem,
                            "sem rede ou sem URL: atualizacao OTA indisponivel");
        }
        if (!r.diagnostico.limpo()) {
            std::snprintf(msg, sizeof msg,
                          "ignorado: %u sem '=', %u chave(s) desconhecida(s), "
                          "%u longo(s), %u indice(s) fora, %u incompleta(s)",
                          static_cast<unsigned>(r.diagnostico.linhas_sem_igual),
                          static_cast<unsigned>(r.diagnostico.chaves_desconhecidas),
                          static_cast<unsigned>(r.diagnostico.valores_longos),
                          static_cast<unsigned>(r.diagnostico.indices_fora),
                          static_cast<unsigned>(r.diagnostico.redes_incompletas));
            logger->warning(kOrigem, msg);
        }
    }
    return r;
}

}  // namespace coruja
