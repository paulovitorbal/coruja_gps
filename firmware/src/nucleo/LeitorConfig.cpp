#include "nucleo/LeitorConfig.h"

#include <cstdio>
#include <cstring>

#include "log/Logger.h"
#include "nucleo/LinhaConfig.h"

namespace coruja {

namespace {

constexpr const char* kOrigem = "config";

/// `true`/`false` ou `1`/`0`, sem diferenciar maiúsculas. Qualquer outra
/// coisa é recusada em vez de interpretada: aceitar "sim" e não "yes", ou o
/// contrário, seria uma regra que ninguém adivinha.
bool booleano(const char* ini, std::size_t n, bool* destino) {
    const auto casa = [&](const char* literal) {
        const std::size_t tam = std::strlen(literal);
        if (tam != n) return false;
        for (std::size_t i = 0; i < n; ++i) {
            char c = ini[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != literal[i]) return false;
        }
        return true;
    };
    if (casa("true") || casa("1"))  { *destino = true;  return true; }
    if (casa("false") || casa("0")) { *destino = false; return true; }
    return false;
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

/// Inteiro sem sinal, sem aceitar lixo à volta.
bool inteiro(const char* ini, std::size_t n, unsigned* destino) {
    if (n == 0 || n > 5) { return false; }
    unsigned v = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (ini[i] < '0' || ini[i] > '9') { return false; }
        v = v * 10U + static_cast<unsigned>(ini[i] - '0');
    }
    *destino = v;
    return true;
}

/// Brilho de **0 a 100**, em passos de 5. Um valor fora do passo seria
/// aceito e depois arredondado em silêncio ao virar índice da curva; recusar
/// deixa o erro visível enquanto ainda dá para corrigir o arquivo.
///
/// **`0` não apaga a tela.** Na escala do arquivo, como na do menu, zero é
/// o mais escuro que este painel consegue mostrar — `kPisoFisicoPct` de
/// luminância, medido no R-05. O piso do hardware não aparece aqui de
/// propósito: ele já mudou uma vez (5% → 10%) e pode mudar com outro
/// painel, e nenhum `coruja.cfg` deveria precisar ser reescrito por isso.
bool le_brilho(const char* ini, std::size_t n, std::uint8_t* destino) {
    unsigned v = 0;
    if (!inteiro(ini, n, &v) || v > 100 || v % 5 != 0) {
        return false;
    }
    *destino = static_cast<std::uint8_t>(v);
    return true;
}

/// Intensidade do LED: mesmos passos de 5 do brilho da tela, **mas com piso
/// em 5 e não em zero**.
///
/// Zero é o que o `led` faz. Aceitá-lo aqui daria dois ajustes capazes de
/// apagar o LED, disputando o mesmo resultado — e quem escrevesse `led_dia=0`
/// acharia que desligou por um caminho que não é o de desligar.
///
/// O literal `5` fica aqui, como o `100` do `le_brilho` logo acima: o leitor
/// de configuração valida com os seus próprios números, e não puxa o módulo
/// do LED para dentro do núcleo só para citar uma constante.
bool le_intensidade_led(const char* ini, std::size_t n,
                        std::uint8_t* destino) {
    std::uint8_t v = 0;
    if (!le_brilho(ini, n, &v) || v < 5) { return false; }
    *destino = v;
    return true;
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

        const ParChaveValor par = divide(linha, n);
        if (par.vazia || par.comentario) {
            continue;
        }
        ++r.diagnostico.linhas_lidas;
        if (!par.valida) {
            ++r.diagnostico.linhas_sem_igual;
            continue;
        }

        const char* chave = par.chave;
        std::size_t nc = par.n_chave;
        const char* valor = par.valor;
        std::size_t nv = par.n_valor;

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
        if (igual(chave, nc, "log_to_sd")) {
            bool valor_lido = false;
            if (booleano(valor, nv, &valor_lido)) {
                r.config.log_para_cartao = valor_lido;
            } else {
                ++r.diagnostico.valores_invalidos;
            }
            continue;
        }

        if (igual(chave, nc, "log_level")) {
            char texto[16] = {};
            if (nv < sizeof texto && nivel_de_texto(
                    (std::memcpy(texto, valor, nv), texto), &r.config.nivel_log)) {
                continue;
            }
            ++r.diagnostico.valores_invalidos;
            continue;
        }

        if (igual(chave, nc, "url_base")) {
            if (!copia(r.config.url_base, kMaxUrl, valor, nv)) {
                ++r.diagnostico.valores_longos;
            }
            continue;
        }

        if (igual(chave, nc, "url_envio")) {
            if (!copia(r.config.url_envio, kMaxUrl, valor, nv)) {
                ++r.diagnostico.valores_longos;
            }
            continue;
        }

        if (igual(chave, nc, "servidor_ntp")) {
            if (!copia(r.config.servidor_ntp, kMaxUrl, valor, nv)) {
                ++r.diagnostico.valores_longos;
            }
            continue;
        }

        if (igual(chave, nc, "token_aparelho")) {
            // Nunca imprimir o valor: e um segredo, como a senha de Wi-Fi.
            if (!copia(r.config.token_aparelho, kMaxToken, valor, nv)) {
                ++r.diagnostico.valores_longos;
            }
            continue;
        }

        if (igual(chave, nc, "nome")) {
            if (!copia(r.config.nome, kMaxNome, valor, nv)) {
                ++r.diagnostico.valores_longos;
            }
            continue;
        }
        if (igual(chave, nc, "brilho_dia")) {
            if (!le_brilho(valor, nv, &r.config.brilho_dia)) {
                ++r.diagnostico.valores_invalidos;
            }
            continue;
        }
        if (igual(chave, nc, "brilho_noite")) {
            if (!le_brilho(valor, nv, &r.config.brilho_noite)) {
                ++r.diagnostico.valores_invalidos;
            }
            continue;
        }
        if (igual(chave, nc, "modo_noturno")) {
            if (igual(valor, nv, "auto")) {
                r.config.modo_noturno = ModoNoturno::Automatico;
            } else if (igual(valor, nv, "dia")) {
                r.config.modo_noturno = ModoNoturno::SempreDia;
            } else if (igual(valor, nv, "noite")) {
                r.config.modo_noturno = ModoNoturno::SempreNoite;
            } else {
                ++r.diagnostico.valores_invalidos;
            }
            continue;
        }
        if (igual(chave, nc, "led_dia")) {
            if (!le_intensidade_led(valor, nv, &r.config.led_dia)) {
                ++r.diagnostico.valores_invalidos;
            }
            continue;
        }
        if (igual(chave, nc, "led_noite")) {
            if (!le_intensidade_led(valor, nv, &r.config.led_noite)) {
                ++r.diagnostico.valores_invalidos;
            }
            continue;
        }
        if (igual(chave, nc, "led")) {
            bool valor_lido = false;
            if (booleano(valor, nv, &valor_lido)) {
                r.config.led_ligado = valor_lido;
            } else {
                ++r.diagnostico.valores_invalidos;
            }
            continue;
        }

        // ⚠️ **Aposentada em 2026-10-19, e lida mesmo assim.** O SFM-27 é um
        // buzzer ativo: ele aceita ligado ou desligado, e não havia volume a
        // controlar. A chave continua nos cartões que já existem, e contá-la
        // como desconhecida faria o log acusar erro de digitação onde não
        // há — treinando quem lê a ignorar o contador que existe justamente
        // para pegar erro de digitação.
        if (igual(chave, nc, "volume_buzzer")) {
            ++r.diagnostico.chaves_aposentadas;
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
        char msg[160];  // 96 truncava desde que entrou o contador de valores invalidos
        std::snprintf(msg, sizeof msg, "%u rede(s) configurada(s)",
                      static_cast<unsigned>(r.config.n_redes));
        // Em `debug`: quem chama já registra o mesmo em `info`, e duas linhas
        // dizendo a mesma coisa treinam o leitor a ignorar o log.
        logger->debug(kOrigem, msg);
        if (!r.config.ota_possivel()) {
            logger->warning(kOrigem,
                            "sem rede ou sem URL: atualizacao OTA indisponivel");
        }
        if (!r.diagnostico.limpo()) {
            std::snprintf(msg, sizeof msg,
                          "ignorado: %u sem '=', %u chave(s) desconhecida(s), "
                          "%u longo(s), %u indice(s) fora, %u incompleta(s), "
                          "%u valor(es) invalido(s)",
                          static_cast<unsigned>(r.diagnostico.linhas_sem_igual),
                          static_cast<unsigned>(r.diagnostico.chaves_desconhecidas),
                          static_cast<unsigned>(r.diagnostico.valores_longos),
                          static_cast<unsigned>(r.diagnostico.indices_fora),
                          static_cast<unsigned>(r.diagnostico.redes_incompletas),
                          static_cast<unsigned>(r.diagnostico.valores_invalidos));
            logger->warning(kOrigem, msg);
        }
        // Linha PROPRIA, e em `info`: chave aposentada nao e erro de quem
        // editou o arquivo -- e uma decisao nossa alcancando um cartao
        // antigo. Misturada com as recusas acima, pareceria defeito; calada,
        // deixaria a linha morta no cartao sem ninguem saber que pode sair.
        if (r.diagnostico.chaves_aposentadas > 0) {
            std::snprintf(msg, sizeof msg,
                          "%u ajuste(s) aposentado(s) no arquivo: lidos e "
                          "ignorados, pode apagar a linha",
                          static_cast<unsigned>(
                              r.diagnostico.chaves_aposentadas));
            logger->info(kOrigem, msg);
        }
    }
    return r;
}

}  // namespace coruja
