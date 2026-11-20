#include "nucleo/LeitorConfig.h"

#include <cstdio>
#include <cstring>

#include "log/Logger.h"
#include "nucleo/LinhaConfig.h"
#include "nucleo/Texto.h"

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
    std::uint32_t v = 0;
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


// ----------------------------------------------------- as chaves, em tabela
//
// 🔴 **Eram trinta blocos `if (igual(...)) { ... continue; }` numa função
// só**, e seis deles diziam literalmente a mesma coisa: copia, e conta como
// longo se não couber. Cada chave nova pedia mais um bloco, e a função
// chegou a CC 54 — ninguém lê uma dessas inteira antes de acrescentar a
// trigésima primeira.
//
// A tabela diz o que cada chave É; o código que a percorre diz o que fazer
// com cada espécie, uma vez só. As três espécies abaixo cobrem quinze das
// dezenove chaves; as outras quatro (as duas de rede, `log_level` e
// `modo_noturno`) têm regra própria e ficam em função própria.

/// Chave que copia texto para um campo de tamanho fixo.
struct CampoTexto {
    const char* chave;
    char*       destino;
    std::size_t capacidade;
};

/// Chave que lê um nível de 0 a 100, com a régua de cada um.
struct CampoNivel {
    const char*   chave;
    std::uint8_t* destino;
    bool (*le)(const char*, std::size_t, std::uint8_t*);
};

/// Chave que lê `true`/`false`.
struct CampoBooleano {
    const char* chave;
    bool*       destino;
};

/// As três tabelas apontam para DENTRO de um `Configuracao`, então elas se
/// montam por chamada — o custo é de ponteiros na pilha, e o ganho é não
/// haver um segundo lugar que saiba onde cada chave mora.
struct Tabelas {
    CampoTexto    textos[6];
    CampoNivel    niveis[4];
    CampoBooleano booleanos[2];
};

Tabelas monta_tabelas(Configuracao* c) {
    return Tabelas{
        {{"url_versao",     c->url_versao,     kMaxUrl},
         {"url_base",       c->url_base,       kMaxUrl},
         {"url_envio",      c->url_envio,      kMaxUrl},
         {"servidor_ntp",   c->servidor_ntp,   kMaxUrl},
         // Nunca imprimir o valor: e um segredo, como a senha de Wi-Fi.
         {"token_aparelho", c->token_aparelho, kMaxToken},
         {"nome",           c->nome,           kMaxNome}},
        {{"brilho_dia",   &c->brilho_dia,   le_brilho},
         {"brilho_noite", &c->brilho_noite, le_brilho},
         {"led_dia",      &c->led_dia,      le_intensidade_led},
         {"led_noite",    &c->led_noite,    le_intensidade_led}},
        {{"log_to_sd", &c->log_para_cartao},
         {"led",       &c->led_ligado}}};
}

/// O que o arquivo diz sobre as redes, antes de compactar. Índices salteados
/// são aceitos, então a lista só se fecha no fim da leitura.
struct Redes {
    Rede brutas[kMaxRedes];
    bool tem_ssid[kMaxRedes] = {};
    bool tem_senha[kMaxRedes] = {};
};

bool trata_ssid(const ParChaveValor& par, Redes* redes, DiagnosticoConfig* d) {
    const int idx = indice_de(par.chave, par.n_chave, "wifi_ssid_");
    if (idx < 0) { return false; }
    if (static_cast<std::size_t>(idx) >= kMaxRedes) {
        ++d->indices_fora;
    } else if (par.n_valor == 0) {
        // `wifi_ssid_1=` e o que sobra de apagar o nome da rede sem apagar a
        // chave. Virava uma rede de nome vazio, com o aparelho tentando
        // associar-se a ela e o diagnostico LIMPO -- nada dizia por que nao
        // havia Wi-Fi. Senha vazia, essa sim, e legitima: rede aberta.
        ++d->valores_invalidos;
    } else if (!copia(redes->brutas[idx].ssid, kMaxSsid, par.valor,
                      par.n_valor)) {
        ++d->valores_longos;
    } else {
        redes->tem_ssid[idx] = true;
    }
    return true;
}

bool trata_senha(const ParChaveValor& par, Redes* redes, DiagnosticoConfig* d) {
    const int idx = indice_de(par.chave, par.n_chave, "wifi_senha_");
    if (idx < 0) { return false; }
    if (static_cast<std::size_t>(idx) >= kMaxRedes) {
        ++d->indices_fora;
    } else if (!copia(redes->brutas[idx].senha, kMaxSenha, par.valor,
                      par.n_valor)) {
        // Nunca imprimir o valor: é a senha.
        ++d->valores_longos;
    } else {
        redes->tem_senha[idx] = true;
    }
    return true;
}

bool trata_texto(const ParChaveValor& par, const Tabelas& t, DiagnosticoConfig* d) {
    for (const CampoTexto& campo : t.textos) {
        if (!igual(par.chave, par.n_chave, campo.chave)) { continue; }
        if (!copia(campo.destino, campo.capacidade, par.valor, par.n_valor)) {
            ++d->valores_longos;
        }
        return true;
    }
    return false;
}

bool trata_nivel(const ParChaveValor& par, const Tabelas& t, DiagnosticoConfig* d) {
    for (const CampoNivel& campo : t.niveis) {
        if (!igual(par.chave, par.n_chave, campo.chave)) { continue; }
        if (!campo.le(par.valor, par.n_valor, campo.destino)) {
            ++d->valores_invalidos;
        }
        return true;
    }
    return false;
}

bool trata_booleano(const ParChaveValor& par, const Tabelas& t,
                    DiagnosticoConfig* d) {
    for (const CampoBooleano& campo : t.booleanos) {
        if (!igual(par.chave, par.n_chave, campo.chave)) { continue; }
        bool lido = false;
        if (booleano(par.valor, par.n_valor, &lido)) {
            *campo.destino = lido;
        } else {
            ++d->valores_invalidos;
        }
        return true;
    }
    return false;
}

bool trata_log_level(const ParChaveValor& par, Configuracao* c,
                     DiagnosticoConfig* d) {
    if (!igual(par.chave, par.n_chave, "log_level")) { return false; }
    char texto[16] = {};
    if (par.n_valor < sizeof texto) {
        std::memcpy(texto, par.valor, par.n_valor);
        if (nivel_de_texto(texto, &c->nivel_log)) { return true; }
    }
    ++d->valores_invalidos;
    return true;
}

bool trata_modo_noturno(const ParChaveValor& par, Configuracao* c,
                        DiagnosticoConfig* d) {
    if (!igual(par.chave, par.n_chave, "modo_noturno")) { return false; }
    if (igual(par.valor, par.n_valor, "auto")) {
        c->modo_noturno = ModoNoturno::Automatico;
    } else if (igual(par.valor, par.n_valor, "dia")) {
        c->modo_noturno = ModoNoturno::SempreDia;
    } else if (igual(par.valor, par.n_valor, "noite")) {
        c->modo_noturno = ModoNoturno::SempreNoite;
    } else {
        ++d->valores_invalidos;
    }
    return true;
}

/// Aplica uma linha já dividida. A ordem é a das espécies, e a última
/// palavra é do contador de chaves desconhecidas.
void aplica_linha(const ParChaveValor& par, const Tabelas& t, Redes* redes,
                  ResultadoConfig* r) {
    DiagnosticoConfig* d = &r->diagnostico;
    if (trata_ssid(par, redes, d)) { return; }
    if (trata_senha(par, redes, d)) { return; }
    if (trata_texto(par, t, d)) { return; }
    if (trata_nivel(par, t, d)) { return; }
    if (trata_booleano(par, t, d)) { return; }
    if (trata_log_level(par, &r->config, d)) { return; }
    if (trata_modo_noturno(par, &r->config, d)) { return; }

    // ⚠️ **Aposentada em 2026-10-19, e lida mesmo assim.** O SFM-27 é um
    // buzzer ativo: ele aceita ligado ou desligado, e não havia volume a
    // controlar. A chave continua nos cartões que já existem, e contá-la
    // como desconhecida faria o log acusar erro de digitação onde não
    // há — treinando quem lê a ignorar o contador que existe justamente
    // para pegar erro de digitação.
    if (igual(par.chave, par.n_chave, "volume_buzzer")) {
        ++d->chaves_aposentadas;
        return;
    }
    ++d->chaves_desconhecidas;
}

/// Compacta preservando a ordem do arquivo, que é a ordem de prioridade.
/// Índices salteados (1 e 3, sem o 2) são aceitos: recusá-los puniria um
/// erro de digitação que não tem consequência nenhuma.
void compacta_redes(const Redes& redes, ResultadoConfig* r) {
    for (std::size_t k = 0; k < kMaxRedes; ++k) {
        if (!redes.tem_ssid[k]) {
            if (redes.tem_senha[k]) { ++r->diagnostico.redes_incompletas; }
            continue;
        }
        r->config.redes[r->config.n_redes++] = redes.brutas[k];
    }
}

/// Quantos bytes pular no início: o BOM do UTF-8 (EF BB BF), que o Bloco de
/// Notas e vários editores gravam. Colado na primeira chave, ele fazia a
/// linha não casar com nada — a rede sumia e o log acusava chave
/// desconhecida, mandando procurar erro de digitação numa linha certa. Só no
/// início: no meio do arquivo aqueles bytes são lixo, e seguem sendo chave
/// desconhecida.
std::size_t pula_bom(const char* texto, std::size_t tamanho) {
    if (tamanho < 3) { return 0; }
    const auto* b = reinterpret_cast<const unsigned char*>(texto);
    return (b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) ? 3 : 0;
}

}  // namespace

ResultadoConfig le_config(const char* texto, std::size_t tamanho,
                          Logger* logger) {
    ResultadoConfig r;
    if (texto == nullptr || tamanho == 0) {
        avisa(logger, "configuracao vazia ou ausente; OTA indisponivel");
        return r;
    }

    Redes redes;
    const Tabelas tabelas = monta_tabelas(&r.config);

    for (std::size_t i = pula_bom(texto, tamanho); i < tamanho;) {
        std::size_t fim = i;
        while (fim < tamanho && texto[fim] != '\n') {
            ++fim;
        }
        const ParChaveValor par = divide(texto + i, fim - i);
        i = fim + 1;

        if (par.vazia || par.comentario) { continue; }
        ++r.diagnostico.linhas_lidas;
        if (!par.valida) {
            ++r.diagnostico.linhas_sem_igual;
            continue;
        }
        aplica_linha(par, tabelas, &redes, &r);
    }

    compacta_redes(redes, &r);

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
