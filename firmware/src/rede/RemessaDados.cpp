#include "rede/RemessaDados.h"

#include <cstdio>
#include <cstring>

#include "log/Logger.h"
#include "nucleo/Crc32.h"

namespace coruja {
namespace {

/// Etiqueta de origem no log, como "base" e "ota" nas outras partes.
constexpr const char* kOrigem = "remessa";

bool digito(char c) { return c >= '0' && c <= '9'; }

/// `AAAAMMDD_HHMMSS.log` — e só essa forma. Conferir a forma, e não só a
/// extensão, é o que impede um `.log` qualquer deixado no cartão por outra
/// ferramenta de ser mandado embora e apagado.
bool e_nome_de_viagem(const char* nome) {
    constexpr std::size_t kTam = 19;  // 8 + 1 + 6 + 4
    if (std::strlen(nome) != kTam) { return false; }
    for (std::size_t i = 0; i < 8; ++i) {
        if (!digito(nome[i])) { return false; }
    }
    if (nome[8] != '_') { return false; }
    for (std::size_t i = 9; i < 15; ++i) {
        if (!digito(nome[i])) { return false; }
    }
    return std::strcmp(nome + 15, ".log") == 0;
}

}  // namespace

const char* descreve(ResultadoRemessa r) {
    switch (r) {
        case ResultadoRemessa::Enviada:        return "tudo entregue e confirmado";
        case ResultadoRemessa::NadaAEnviar:    return "nao havia arquivo para enviar";
        case ResultadoRemessa::Parcial:        return "parte entregue; o resto fica para a proxima";
        case ResultadoRemessa::SemConfiguracao:
            return "falta url_envio, token_aparelho ou rede no coruja.cfg";
        case ResultadoRemessa::FalhaDeRede:    return "nao conectou";
        case ResultadoRemessa::FalhaDeCartao:  return "nao consegui listar o cartao";
        case ResultadoRemessa::FalhaAoEnviar:  return "havia o que mandar e nada completou";
    }
    return "resultado desconhecido";
}

const char* descreve_curto(ResultadoRemessa r) {
    // Cabe na faixa de 26 caracteres da tela. Cada falha tem texto proprio
    // pelo mesmo motivo do OTA: sem rede, sem configuracao e cartao ilegivel
    // se resolvem de jeitos diferentes.
    switch (r) {
        case ResultadoRemessa::Enviada:         return "ENVIADO";
        case ResultadoRemessa::NadaAEnviar:     return "NADA A ENVIAR";
        case ResultadoRemessa::Parcial:         return "ENVIO INCOMPLETO";
        case ResultadoRemessa::SemConfiguracao: return "SEM CONFIGURACAO";
        case ResultadoRemessa::FalhaDeRede:     return "SEM REDE";
        case ResultadoRemessa::FalhaDeCartao:   return "CARTAO ILEGIVEL";
        case ResultadoRemessa::FalhaAoEnviar:   return "SERVIDOR NAO ACEITOU";
    }
    return "ERRO";
}

const char* descreve(FaseRemessa f) {
    switch (f) {
        case FaseRemessa::Conectando:  return "conectando";
        case FaseRemessa::Listando:    return "listando o cartao";
        case FaseRemessa::Enviando:    return "enviando";
        case FaseRemessa::Confirmando: return "confirmando";
        case FaseRemessa::Apagando:    return "apagando";
        case FaseRemessa::Concluida:   return "concluida";
        case FaseRemessa::Falhou:      return "falhou";
    }
    return "fase desconhecida";
}

bool nome_enviavel(const char* nome) {
    if (nome == nullptr) { return false; }
    return std::strcmp(nome, "coruja.log") == 0
        || std::strcmp(nome, "infracoes.log") == 0
        || e_nome_de_viagem(nome);
}

bool RemessaDados::url_do_arquivo(const char* base, const char* nome,
                                  Url* destino) {
    if (analisa_url(base, destino) != ErroUrl::Nenhum) { return false; }

    const std::size_t n = std::strlen(destino->caminho);
    // A URL configurada pode ou nao terminar em barra. Normalizar aqui, e nao
    // exigir uma das duas formas no cfg, evita o erro mais provavel de quem
    // edita o arquivo a mao -- e que produziria "/enviocoruja.log".
    const bool tem_barra = n > 0 && destino->caminho[n - 1] == '/';
    const std::size_t precisa = n + (tem_barra ? 0 : 1) + std::strlen(nome);
    if (precisa > kMaxUrl) { return false; }

    std::snprintf(destino->caminho + n, kMaxUrl + 1 - n, "%s%s",
                  tem_barra ? "" : "/", nome);
    return true;
}

void RemessaDados::ao_listar(void* contexto, const char* nome,
                             std::size_t tamanho) {
    auto* c = static_cast<Colheita*>(contexto);
    if (!nome_enviavel(nome)) { return; }
    // Vazio nao se manda: o servidor recusa corpo de zero byte com 400, e
    // insistir a cada remessa so gastaria rede. Ele tambem nao se apaga, para
    // que a regra "so apaga o que foi confirmado" nao tenha excecao.
    if (tamanho == 0) { return; }
    if (std::strlen(nome) > kMaxNomeArquivo) { return; }
    if (c->n >= kMaxRemessa) { ++c->ignorados; return; }
    std::snprintf(c->nomes[c->n], kMaxNomeArquivo + 1, "%s", nome);
    ++c->n;
}

std::size_t RemessaDados::ao_pedir(void* contexto, std::uint8_t* destino,
                                   std::size_t capacidade) {
    auto* b = static_cast<Bomba*>(contexto);
    if (b->falhou || b->entregues >= b->total) { return 0; }

    const std::size_t falta = b->total - b->entregues;
    const std::size_t quer = capacidade < falta ? capacidade : falta;
    std::size_t lidos = 0;
    if (!b->cartao->le(destino, quer, &lidos) || lidos == 0) {
        // Devolver 0 aqui encerraria o corpo em silencio, e o servidor
        // recusaria por Content-Length nao cumprido -- o que daria no mesmo.
        // A marca existe para que o LOG diga "o cartao falhou", e nao "o
        // servidor recusou": sao defeitos diferentes com conserto diferente.
        b->falhou = true;
        return 0;
    }
    b->entregues += lidos;
    if (b->observador != nullptr) {
        b->observador->progresso(b->entregues, b->total);
    }
    return lidos;
}

bool RemessaDados::despacha(const Configuracao& cfg, const char* nome,
                            unsigned indice, unsigned total, Logger& log) {
    char msg[96] = {};

    Url url;
    if (!url_do_arquivo(cfg.url_envio, nome, &url)) {
        std::snprintf(msg, sizeof msg, "url_envio + %s nao forma URL valida",
                      nome);
        log.error(kOrigem, msg);
        return false;
    }

    std::size_t tamanho = 0;
    if (cartao_.abre_para_leitura(nome, &tamanho, log) != ErroCartao::Nenhum) {
        std::snprintf(msg, sizeof msg, "nao abri %s", nome);
        log.error(kOrigem, msg);
        return false;
    }

    // --- primeira passada: o CRC do que esta no cartao agora ---
    Crc32 crc;
    std::size_t somados = 0;
    bool leitura_ok = true;
    while (somados < tamanho) {
        std::size_t lidos = 0;
        if (!cartao_.le(bomba_.pedaco, sizeof bomba_.pedaco, &lidos)
                || lidos == 0) {
            leitura_ok = false;
            break;
        }
        crc.alimenta(bomba_.pedaco, lidos);
        somados += lidos;
    }
    if (!leitura_ok || somados != tamanho) {
        cartao_.fecha_leitura();
        std::snprintf(msg, sizeof msg, "%s ilegivel (%u de %u B)", nome,
                      static_cast<unsigned>(somados),
                      static_cast<unsigned>(tamanho));
        log.error(kOrigem, msg);
        return false;
    }
    const std::uint32_t crc_local = crc.valor();

    if (!cartao_.rebobina()) {
        cartao_.fecha_leitura();
        std::snprintf(msg, sizeof msg, "nao rebobinei %s", nome);
        log.error(kOrigem, msg);
        return false;
    }

    // --- segunda passada: o envio ---
    if (observador_ != nullptr) {
        observador_->fase(FaseRemessa::Enviando, nome, indice, total);
    }
    bomba_.cartao = &cartao_;
    bomba_.observador = observador_;
    bomba_.entregues = 0;
    bomba_.total = tamanho;
    bomba_.falhou = false;

    const auto envio = http_.envia(url, cfg.token_aparelho, ao_pedir, &bomba_,
                                   tamanho, log, kTempoLimiteEnvioMs);
    // Fechar ANTES de falar com o servidor de novo: o cartao e removivel, e
    // manter um volume montado por toda uma consulta de rede e exatamente como
    // se corrompe um sistema de arquivos.
    cartao_.fecha_leitura();

    if (bomba_.falhou) {
        std::snprintf(msg, sizeof msg,
                      "leitura de %s falhou no meio do envio", nome);
        log.error(kOrigem, msg);
        return false;
    }
    if (!envio.ok()) {
        std::snprintf(msg, sizeof msg, "%s nao subiu: %s (status %u)", nome,
                      descreve(envio.erro),
                      static_cast<unsigned>(envio.status));
        log.error(kOrigem, msg);
        return false;
    }

    // --- o servidor confirma o que GUARDOU ---
    if (observador_ != nullptr) {
        observador_->fase(FaseRemessa::Confirmando, nome, indice, total);
    }
    std::uint32_t crc_remoto = 0;
    const auto consulta = http_.consulta_crc(url, cfg.token_aparelho, &crc_remoto,
                                             log, kTempoLimiteConsultaMs);
    if (!consulta.ok()) {
        std::snprintf(msg, sizeof msg, "%s subiu mas nao confirmou: %s",
                      nome, descreve(consulta.erro));
        log.warning(kOrigem, msg);
        return false;
    }
    if (crc_remoto != crc_local) {
        std::snprintf(msg, sizeof msg,
                      "%s chegou diferente (local %08x, servidor %08x)", nome,
                      static_cast<unsigned>(crc_local),
                      static_cast<unsigned>(crc_remoto));
        log.warning(kOrigem, msg);
        return false;
    }

    ++entregues_;

    // --- o arquivo cresceu desde a soma do CRC? ---
    std::size_t agora = 0;
    const bool releu = cartao_.abre_para_leitura(nome, &agora, log)
                       == ErroCartao::Nenhum;
    cartao_.fecha_leitura();
    if (!releu || agora != tamanho) {
        // Nao e falha: o conteudo esta a salvo no servidor. Apagar e que
        // destruiria os bytes escritos durante o envio.
        ++retidos_;
        std::snprintf(msg, sizeof msg,
                      "%s guardado no servidor, mantido aqui (era %u B, "
                      "agora %u)", nome, static_cast<unsigned>(tamanho),
                      static_cast<unsigned>(agora));
        log.info(kOrigem, msg);
        return true;
    }

    if (observador_ != nullptr) {
        observador_->fase(FaseRemessa::Apagando, nome, indice, total);
    }
    if (cartao_.remove(nome, log) != ErroCartao::Nenhum) {
        ++retidos_;
        std::snprintf(msg, sizeof msg,
                      "%s guardado no servidor, mas nao consegui apagar",
                      nome);
        log.warning(kOrigem, msg);
        return true;
    }
    std::snprintf(msg, sizeof msg, "%s entregue (%u B, crc %08x) e apagado",
                  nome, static_cast<unsigned>(tamanho),
                  static_cast<unsigned>(crc_local));
    log.info(kOrigem, msg);
    return true;
}

ResultadoRemessa RemessaDados::executa(const Configuracao& cfg, Logger& log) {
    entregues_ = falhados_ = retidos_ = 0;

    if (!cfg.envio_possivel()) {
        log.warning(kOrigem,
                    "sem url_envio, token_aparelho ou rede no coruja.cfg");
        return ResultadoRemessa::SemConfiguracao;
    }

    if (observador_ != nullptr) {
        observador_->fase(FaseRemessa::Conectando, "", 0, 0);
    }
    const auto erro_rede = rede_.conecta(cfg, log);
    if (erro_rede != ErroWifi::Nenhum) {
        char msg[96] = {};
        std::snprintf(msg, sizeof msg, "sem rede: %s", descreve(erro_rede));
        log.error(kOrigem, msg);
        if (observador_ != nullptr) {
            observador_->fase(FaseRemessa::Falhou, "", 0, 0);
        }
        return ResultadoRemessa::FalhaDeRede;
    }

    // AGORA, e nao antes: o `conecta()` acima e quem chama o
    // `cyw43_arch_init()`. Ver `PreparoDeSessao`.
    if (preparo_ != nullptr) {
        preparo_->apos_conectar(log);
    }

    ResultadoRemessa saida = ResultadoRemessa::NadaAEnviar;
    Colheita colheita;

    if (observador_ != nullptr) {
        observador_->fase(FaseRemessa::Listando, "", 0, 0);
    }
    if (cartao_.lista(ao_listar, &colheita, log) != ErroCartao::Nenhum) {
        log.error(kOrigem, "nao consegui listar o cartao");
        saida = ResultadoRemessa::FalhaDeCartao;
    } else {
        if (colheita.ignorados > 0) {
            char msg[64] = {};
            std::snprintf(msg, sizeof msg,
                          "%u arquivos ficaram para a proxima",
                          static_cast<unsigned>(colheita.ignorados));
            log.info(kOrigem, msg);
        }
        const auto total = static_cast<unsigned>(colheita.n);
        for (std::size_t i = 0; i < colheita.n; ++i) {
            if (!despacha(cfg, colheita.nomes[i],
                          static_cast<unsigned>(i) + 1, total, log)) {
                ++falhados_;
            }
        }
        if (total == 0) {
            saida = ResultadoRemessa::NadaAEnviar;
        } else if (falhados_ == 0) {
            // Sobrar arquivo para a proxima nao e tudo entregue.
            saida = colheita.ignorados > 0 ? ResultadoRemessa::Parcial
                                           : ResultadoRemessa::Enviada;
        } else if (entregues_ == 0) {
            saida = ResultadoRemessa::FalhaAoEnviar;
        } else {
            saida = ResultadoRemessa::Parcial;
        }
    }

    // Desconectar SEMPRE, inclusive apos falha de cartao: o radio ligado
    // consome e nao serve mais a ninguem.
    rede_.desconecta(log);

    if (observador_ != nullptr) {
        observador_->fase(e_falha(saida) ? FaseRemessa::Falhou
                                         : FaseRemessa::Concluida,
                          "", 0, 0);
    }
    char fim[96] = {};
    std::snprintf(fim, sizeof fim, "%s (%u entregues, %u retidos, %u falhados)",
                  descreve(saida), entregues_, retidos_, falhados_);
    log.info(kOrigem, fim);
    return saida;
}

}  // namespace coruja
