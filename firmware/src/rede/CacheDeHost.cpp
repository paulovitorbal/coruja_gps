#include "rede/CacheDeHost.h"

#include <cstdio>
#include <cstring>

namespace coruja {

bool CacheDeHost::serve(const char* host, std::uint32_t agora_ms) const {
    if (!tem_ || host == nullptr || host[0] == '\0') { return false; }
    if (std::strcmp(host_, host) != 0) { return false; }
    return idade(agora_ms) < validade_ms_;
}

void CacheDeHost::guarda(const char* host, std::uint32_t agora_ms) {
    // O host vazio: a guarda é redundante com o `serve`, que já recusa `""`
    // — uma campanha de mutação confirmou que tirá-la não muda nada
    // observável. Fica porque mantém a invariante legível: `tem_` verdadeiro
    // quer dizer que há um nome ali, e quem ler a classe não precisa ir ao
    // `serve` descobrir que às vezes não há.
    if (host == nullptr || host[0] == '\0') { esquece(); return; }
    // Comparar ANTES de copiar: um nome que não cabe tem de deixar o cache
    // vazio, e não meio escrito.
    if (std::strlen(host) > kMaxUrl) { esquece(); return; }
    std::snprintf(host_, sizeof host_, "%s", host);
    guardado_em_ms_ = agora_ms;
    tem_ = true;
}

void CacheDeHost::esquece() {
    tem_ = false;
    host_[0] = '\0';
}

}  // namespace coruja
