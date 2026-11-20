#include "rede/RemessaDados.h"

#include <cstdio>
#include <cstring>

#include "log/Logger.h"
#include "nucleo/CaoSuspenso.h"
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
        case ResultadoRemessa::EnviadaFaltaMais:
            return "o que coube foi entregue; o resto fica para a proxima";
        case ResultadoRemessa::NaoApagou:
            return "entregue e confirmado, mas o cartao nao soltou o arquivo";
        case ResultadoRemessa::Parcial:        return "parte entregue; o resto fica para a proxima";
        case ResultadoRemessa::SemConfiguracao:
            return "url_envio, token_aparelho ou rede faltando ou invalido "
                   "no coruja.cfg";
        case ResultadoRemessa::FalhaDeRede:    return "nao conectou";
        case ResultadoRemessa::FalhaDeCartao:  return "nao consegui ler o cartao";
        case ResultadoRemessa::FalhaAoEnviar:
            return "o servidor respondeu e nada foi aceito";
        case ResultadoRemessa::SemResposta:
            return "o servidor nao respondeu (DNS, conexao ou tempo esgotado)";
        case ResultadoRemessa::CertificadoRecusado:
            return "o certificado do servidor foi recusado (relogio acertado?)";
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
        case ResultadoRemessa::EnviadaFaltaMais: return "ENVIADO; FALTA MAIS";
        case ResultadoRemessa::NaoApagou:       return "ENTREGUE; NAO APAGUEI";
        case ResultadoRemessa::Parcial:         return "ENVIO INCOMPLETO";
        case ResultadoRemessa::SemConfiguracao: return "SEM CONFIGURACAO";
        case ResultadoRemessa::FalhaDeRede:     return "SEM REDE";
        case ResultadoRemessa::FalhaDeCartao:   return "CARTAO ILEGIVEL";
        case ResultadoRemessa::FalhaAoEnviar:   return "SERVIDOR NAO ACEITOU";
        case ResultadoRemessa::SemResposta:     return "SERVIDOR NAO RESPONDEU";
        case ResultadoRemessa::CertificadoRecusado:
            return "CERTIFICADO RECUSADO";
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

    // `url_envio` com consulta nao forma URL com o nome no fim: ele cairia
    // DENTRO da consulta -- "/envio?t=1/coruja.log" --, o servidor nao
    // acharia a rota, e o arquivo ficaria tentando a cada remessa. Recusar
    // aqui aponta o coruja.cfg, que e onde se conserta.
    if (std::strchr(destino->caminho, '?') != nullptr) { return false; }

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
    // **O progresso do envio: bytes que sairam do cartao.** Um PUT que gasta
    // a janela de inatividade inteira sem puxar um pedaco nao passa por aqui
    // uma vez sequer -- e e assim que o cao morde um transporte travado.
    if (b->cao != nullptr) { b->cao->alimenta(); }
    if (b->observador != nullptr) {
        b->observador->progresso(b->entregues, b->total);
    }
    return lidos;
}

void RemessaDados::avisa(FaseRemessa fase, const char* nome, unsigned indice,
                         unsigned total, unsigned tentativa) {
    if (observador_ != nullptr) {
        observador_->fase(fase, nome, indice, total, tentativa);
    }
}

RemessaDados::Desfecho RemessaDados::classifica(const ResultadoHttp& r) {
    // A leitura do resultado e a do OTA tambem: mora em `classifica_falha`.
    switch (classifica_falha(r)) {
        case FalhaHttp::Url:            return Desfecho::Url;
        case FalhaHttp::SemResposta:    return Desfecho::SemResposta;
        case FalhaHttp::ErroNoServidor: return Desfecho::ErroNoServidor;
        case FalhaHttp::GrandeDemais:   return Desfecho::GrandeDemais;
        case FalhaHttp::Recusado:       return Desfecho::Recusado;
        case FalhaHttp::Certificado:    return Desfecho::Certificado;
    }
    return Desfecho::SemResposta;  // fora de faixa: o lado que insiste
}

bool RemessaDados::vale_repetir(Desfecho d) {
    switch (d) {
        // O que pode mudar em dois segundos: o cartao que solucou, a rede que
        // voltou, o servidor que se recompos -- e o CRC diferente, que se
        // conserta reenviando (ver `confirma`).
        case Desfecho::Cartao:
        case Desfecho::Divergiu:
        case Desfecho::SemResposta:
        case Desfecho::ErroNoServidor:
            return true;
        // O que voltaria igual. O certificado entra aqui porque o relogio nao
        // anda durante a remessa: o GPS nao e lido, e o NTP ja foi tentado
        // no `PreparoDeSessao`.
        case Desfecho::Entregue:
        case Desfecho::Url:
        case Desfecho::GrandeDemais:
        case Desfecho::Recusado:
        case Desfecho::Certificado:
            return false;
    }
    return false;  // fora de faixa: nao insistir e o lado que nao prende
}

bool RemessaDados::aponta_o_caminho(Desfecho d) {
    switch (d) {
        // DNS, rede, relogio, token, URL do servidor: o arquivo seguinte
        // passaria pelo mesmo lugar. PODE ser so deste -- o tempo esgotado de
        // um arquivo grande, o 404 de um nome --, e e por isso que a conta
        // pede dois seguidos (`kFalhasSeguidasAteParar`).
        case Desfecho::SemResposta:
        case Desfecho::ErroNoServidor:
        case Desfecho::Recusado:
        case Desfecho::Certificado:
            return true;
        // So deste arquivo, com certeza. Nem entram na conta: o cartao e a
        // URL nem chegaram a rede, e o 413 e o CRC diferente vieram de um
        // servidor que respondeu.
        case Desfecho::Entregue:
        case Desfecho::Cartao:
        case Desfecho::Url:
        case Desfecho::Divergiu:
        case Desfecho::GrandeDemais:
            return false;
    }
    return true;  // fora de faixa: contar e o lado que nao prende o aparelho
}

bool RemessaDados::prova_o_caminho(Desfecho d) {
    switch (d) {
        // O servidor respondeu a ESTE arquivo: DNS, conexao, certificado e
        // token passaram. O 413 e o CRC diferente sao so do arquivo, e dizem
        // que o seguinte encontraria o caminho aberto.
        case Desfecho::Entregue:
        case Desfecho::GrandeDemais:
        case Desfecho::Divergiu:
            return true;
        // A recusa e o erro do servidor tambem vieram dele, mas sao do
        // caminho -- token, `url_envio`, servidor caido -- e o seguinte daria
        // no mesmo. O cartao e a URL nem chegaram a rede: nao dizem nada do
        // caminho, nem para o bem nem para o mal.
        case Desfecho::Cartao:
        case Desfecho::Url:
        case Desfecho::SemResposta:
        case Desfecho::ErroNoServidor:
        case Desfecho::Recusado:
        case Desfecho::Certificado:
            return false;
    }
    return false;  // fora de faixa: nao zerar e o lado que nao prende
}

ResultadoRemessa RemessaDados::sem_entrega(Desfecho d) {
    switch (d) {
        // Os que a tela nao pode dizer como SERVIDOR NAO ACEITOU: ele nao
        // respondeu, ou nem chegou a ser procurado.
        case Desfecho::SemResposta: return ResultadoRemessa::SemResposta;
        case Desfecho::Certificado: return ResultadoRemessa::CertificadoRecusado;
        case Desfecho::Cartao:      return ResultadoRemessa::FalhaDeCartao;
        case Desfecho::Url:         return ResultadoRemessa::SemConfiguracao;
        // Aqui ele respondeu, e nao aceitou.
        case Desfecho::Divergiu:
        case Desfecho::GrandeDemais:
        case Desfecho::ErroNoServidor:
        case Desfecho::Recusado:
        case Desfecho::Entregue:  // nao chega aqui: so vem o que falhou
            return ResultadoRemessa::FalhaAoEnviar;
    }
    return ResultadoRemessa::FalhaAoEnviar;
}

RemessaDados::Desfecho RemessaDados::confirma(const Url& url,
                                              const Configuracao& cfg,
                                              const char* nome,
                                              std::uint32_t crc_local,
                                              unsigned indice, unsigned total,
                                              unsigned tentativa,
                                              Logger& log) {
    char msg[112] = {};
    Desfecho d = Desfecho::SemResposta;
    for (unsigned t = 1; t <= kTentativasDeConfirmacao; ++t) {
        if (t > 1) {
            // O teto do conjunto vale aqui tambem: insistir numa pergunta
            // depois que o tempo acabou so atrasa a tela.
            if (esgotou_o_tempo()) { break; }
            pausa_.espera_ms(kEsperaEntreTentativasRemessaMs);
            alimenta_o_cao();
        }
        avisa(FaseRemessa::Confirmando, nome, indice, total, tentativa);

        std::uint32_t crc_remoto = 0;
        alimenta_o_cao();
        const auto consulta = http_.consulta_crc(url, cfg.token_aparelho,
                                                 &crc_remoto, log,
                                                 kTempoLimiteConsultaMs);
        alimenta_o_cao();
        if (consulta.ok()) {
            if (crc_remoto == crc_local) { return Desfecho::Entregue; }
            // CRC diferente NAO se resolve perguntando de novo: o que esta
            // guardado nao e o que foi mandado, e a pergunta seguinte traria
            // a mesma resposta. Quem conserta isto e reenviar o arquivo --
            // que e a tentativa de fora, nao esta.
            std::snprintf(msg, sizeof msg,
                          "%s chegou diferente (local %08x, servidor %08x)",
                          nome, static_cast<unsigned>(crc_local),
                          static_cast<unsigned>(crc_remoto));
            log.warning(kOrigem, msg);
            return Desfecho::Divergiu;
        }
        // O status vai junto, como no PUT: um 401 e um 503 aqui pedem
        // consertos diferentes, e so o codigo os separa no remessa.log.
        std::snprintf(msg, sizeof msg,
                      "%s subiu mas nao confirmou (%u de %u): %s (status %u)",
                      nome, t, kTentativasDeConfirmacao,
                      descreve(consulta.erro),
                      static_cast<unsigned>(consulta.status));
        log.warning(kOrigem, msg);
        // A recusa tambem nao muda perguntando de novo, pelo motivo do CRC
        // diferente: o servidor respondeu, e responderia o mesmo.
        d = classifica(consulta);
        if (!vale_repetir(d)) { return d; }
    }
    return d;
}

RemessaDados::Desfecho RemessaDados::insiste(const Configuracao& cfg,
                                             const char* nome,
                                             unsigned indice, unsigned total,
                                             Logger& log) {
    char msg[112] = {};
    Desfecho d = Desfecho::SemResposta;
    for (unsigned t = 1; t <= kTentativasPorArquivo; ++t) {
        if (t > 1) {
            if (esgotou_o_tempo()) {
                std::snprintf(msg, sizeof msg,
                              "%s: o tempo da remessa acabou entre tentativas",
                              nome);
                log.warning(kOrigem, msg);
                return d;
            }
            std::snprintf(msg, sizeof msg,
                          "%s: tentativa %u de %u, apos %lu ms", nome, t,
                          kTentativasPorArquivo,
                          static_cast<unsigned long>(kEsperaEntreTentativasRemessaMs));
            log.warning(kOrigem, msg);
            pausa_.espera_ms(kEsperaEntreTentativasRemessaMs);
            // Depois da espera: a tentativa seguinte comecou, e e ela o
            // progresso. Sem isto a espera se somaria a inatividade do
            // pedido anterior.
            alimenta_o_cao();
        }
        d = despacha(cfg, nome, indice, total, t, log);
        if (d == Desfecho::Entregue) { return d; }
        if (!vale_repetir(d)) {
            std::snprintf(msg, sizeof msg,
                          "%s: tentar de novo daria no mesmo; nao repito",
                          nome);
            log.warning(kOrigem, msg);
            return d;
        }
    }
    return d;
}

RemessaDados::Desfecho RemessaDados::despacha(const Configuracao& cfg,
                                              const char* nome,
                                              unsigned indice, unsigned total,
                                              unsigned tentativa,
                                              Logger& log) {
    char msg[96] = {};

    Url url;
    if (!url_do_arquivo(cfg.url_envio, nome, &url)) {
        std::snprintf(msg, sizeof msg, "url_envio + %s nao forma URL valida",
                      nome);
        log.error(kOrigem, msg);
        return Desfecho::Url;
    }

    std::size_t tamanho = 0;
    if (cartao_.abre_para_leitura(nome, &tamanho, log) != ErroCartao::Nenhum) {
        std::snprintf(msg, sizeof msg, "nao abri %s", nome);
        log.error(kOrigem, msg);
        return Desfecho::Cartao;
    }
    if (tamanho == 0) {
        // A listagem ja pula o vazio; este esvaziou depois dela. O cliente
        // nem manda corpo de zero byte -- devolveria `NaoIniciou`, que
        // contaria como rede morta por culpa do arquivo.
        cartao_.fecha_leitura();
        std::snprintf(msg, sizeof msg, "%s ficou vazio depois de listado",
                      nome);
        log.error(kOrigem, msg);
        return Desfecho::Cartao;
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
        // A primeira passada le o arquivo inteiro do cartao, e um
        // `coruja.log` de 28 KB sao 28 leituras. Cada pedaco lido e
        // progresso; um `le()` que nunca volte nao produz nenhum.
        alimenta_o_cao();
    }
    if (!leitura_ok || somados != tamanho) {
        cartao_.fecha_leitura();
        std::snprintf(msg, sizeof msg, "%s ilegivel (%u de %u B)", nome,
                      static_cast<unsigned>(somados),
                      static_cast<unsigned>(tamanho));
        log.error(kOrigem, msg);
        return Desfecho::Cartao;
    }
    const std::uint32_t crc_local = crc.valor();

    if (!cartao_.rebobina()) {
        cartao_.fecha_leitura();
        std::snprintf(msg, sizeof msg, "nao rebobinei %s", nome);
        log.error(kOrigem, msg);
        return Desfecho::Cartao;
    }

    // --- segunda passada: o envio ---
    avisa(FaseRemessa::Enviando, nome, indice, total, tentativa);
    bomba_.cartao = &cartao_;
    bomba_.observador = observador_;
    bomba_.cao = cao_;
    bomba_.entregues = 0;
    bomba_.total = tamanho;
    bomba_.falhou = false;

    alimenta_o_cao();
    const auto envio = http_.envia(url, cfg.token_aparelho, ao_pedir, &bomba_,
                                   tamanho, log, kTempoLimiteEnvioMs);
    // O pedido voltou, qualquer que tenha sido o desfecho. Por dentro dele
    // quem alimenta e a bomba, e so com pedaco entregue.
    alimenta_o_cao();
    // Fechar ANTES de falar com o servidor de novo: o cartao e removivel, e
    // manter um volume montado por toda uma consulta de rede e exatamente como
    // se corrompe um sistema de arquivos.
    cartao_.fecha_leitura();

    if (bomba_.falhou) {
        std::snprintf(msg, sizeof msg,
                      "leitura de %s falhou no meio do envio", nome);
        log.error(kOrigem, msg);
        return Desfecho::Cartao;
    }
    if (!envio.ok()) {
        std::snprintf(msg, sizeof msg, "%s nao subiu: %s (status %u)", nome,
                      descreve(envio.erro),
                      static_cast<unsigned>(envio.status));
        log.error(kOrigem, msg);
        return classifica(envio);
    }

    // --- o servidor confirma o que GUARDOU ---
    const Desfecho confirmacao = confirma(url, cfg, nome, crc_local, indice,
                                          total, tentativa, log);
    if (confirmacao != Desfecho::Entregue) { return confirmacao; }

    ++entregues_;

    // --- o que esta no cartao ainda e o que subiu? ---
    //
    // Pelo TAMANHO so, isto deixava passar o arquivo trocado por outro do
    // mesmo tamanho -- e "so apaga o que o servidor confirmou" vale pelo
    // CONTEUDO, nao pelo numero de bytes. A conferencia custa uma terceira
    // leitura do arquivo; ela paga por nao destruir o unico exemplar de
    // algo que o servidor nunca viu.
    std::size_t agora = 0;
    const bool releu = cartao_.abre_para_leitura(nome, &agora, log)
                       == ErroCartao::Nenhum;
    Crc32 confere;
    bool  igual = releu && agora == tamanho;
    if (igual) {
        std::size_t relidos = 0;
        while (relidos < tamanho) {
            std::size_t n = 0;
            if (!cartao_.le(bomba_.pedaco, sizeof bomba_.pedaco, &n)
                    || n == 0) {
                igual = false;
                break;
            }
            confere.alimenta(bomba_.pedaco, n);
            relidos += n;
        }
        igual = igual && relidos == tamanho && confere.valor() == crc_local;
    }
    cartao_.fecha_leitura();
    if (!igual) {
        // Nao e falha: o conteudo esta a salvo no servidor. Apagar e que
        // destruiria os bytes escritos durante o envio -- ou um arquivo
        // que nunca subiu.
        ++retidos_;
        std::snprintf(msg, sizeof msg,
                      "%s guardado no servidor, mantido aqui (era %u B, "
                      "agora %u; crc %s)", nome,
                      static_cast<unsigned>(tamanho),
                      static_cast<unsigned>(agora),
                      agora == tamanho ? "DIFERE" : "nao conferido");
        log.info(kOrigem, msg);
        return Desfecho::Entregue;
    }

    avisa(FaseRemessa::Apagando, nome, indice, total, tentativa);
    if (cartao_.remove(nome, log) != ErroCartao::Nenhum) {
        ++retidos_;
        std::snprintf(msg, sizeof msg,
                      "%s guardado no servidor, mas nao consegui apagar",
                      nome);
        log.warning(kOrigem, msg);
        return Desfecho::Entregue;
    }
    std::snprintf(msg, sizeof msg, "%s entregue (%u B, crc %08x) e apagado",
                  nome, static_cast<unsigned>(tamanho),
                  static_cast<unsigned>(crc_local));
    log.info(kOrigem, msg);
    return Desfecho::Entregue;
}

ResultadoRemessa RemessaDados::executa(const Configuracao& cfg, Logger& log) {
    entregues_ = falhados_ = retidos_ = restantes_ = 0;
    // O relogio parte ANTES de conectar: a varredura e a associacao do Wi-Fi
    // sao dezenas de segundos de aparelho parado, e quem promete um teto para
    // o conjunto nao pode comecar a contar depois da parte mais lenta.
    inicio_ms_ = pausa_.agora_ms();

    if (!cfg.envio_possivel()) {
        log.warning(kOrigem,
                    "sem url_envio, token_aparelho ou rede no coruja.cfg");
        return ResultadoRemessa::SemConfiguracao;
    }

    avisa(FaseRemessa::Conectando, "", 0, 0, 1);
    // ⚠️ Cao SOLTO na associacao, pelo mesmo motivo do OTA: a varredura
    // (12 s) e a associacao (20 s) sao chamadas do SDK sem gancho por
    // dentro, e a segunda passa do teto de 16,777 s do hardware. Ver
    // `AtualizadorOta::executa`.
    ErroWifi erro_rede;
    {
        // RAII: a devolucao do cao nao depende de ninguem lembrar do retoma.
        const CaoSuspenso solto(cao_);
        erro_rede = rede_.conecta(cfg, log);
    }
    if (erro_rede != ErroWifi::Nenhum) {
        char msg[96] = {};
        std::snprintf(msg, sizeof msg, "sem rede: %s", descreve(erro_rede));
        log.error(kOrigem, msg);
        avisa(FaseRemessa::Falhou, "", 0, 0, 1);
        return ResultadoRemessa::FalhaDeRede;
    }

    // AGORA, e nao antes: o `conecta()` acima e quem chama o
    // `cyw43_arch_init()`. Ver `PreparoDeSessao`.
    if (preparo_ != nullptr) {
        preparo_->apos_conectar(log);
    }

    ResultadoRemessa saida = ResultadoRemessa::NadaAEnviar;
    // So os contadores: os nomes alem de `n` nao sao lidos por ninguem, e
    // limpar 1280 B a cada remessa seria trabalho para nao mudar nada.
    colheita_.n = 0;
    colheita_.ignorados = 0;

    avisa(FaseRemessa::Listando, "", 0, 0, 1);
    if (cartao_.lista(ao_listar, &colheita_, log) != ErroCartao::Nenhum) {
        log.error(kOrigem, "nao consegui listar o cartao");
        saida = ResultadoRemessa::FalhaDeCartao;
    } else {
        // O laco mora aqui, e nao numa funcao propria, por causa da pilha:
        // separado, o `msg` dele deixa de dividir a vaga com o `fim` la de
        // baixo, e o caminho mais fundo da remessa -- o que desce ate o
        // aperto de mao do mbedTLS -- ganhava 144 B. Medido com
        // `-fstack-usage` em 16/11/2026.
        char msg[96] = {};
        restantes_ = static_cast<unsigned>(colheita_.ignorados);
        if (colheita_.ignorados > 0) {
            std::snprintf(msg, sizeof msg,
                          "%u arquivos nao couberam e ficam para a proxima",
                          static_cast<unsigned>(colheita_.ignorados));
            log.info(kOrigem, msg);
        }
        const auto total = static_cast<unsigned>(colheita_.n);
        // O motivo de uma remessa sem entrega, que e o que a tela diz ao
        // usuario. A falha do caminho vale mais que a do arquivo, porque e
        // ela que pede conserto: um 401 seguido de um cartao que solucou e
        // token errado, e nao CARTAO ILEGIVEL. Entre iguais, a mais recente.
        Desfecho motivo = Desfecho::Entregue;
        // Arquivos que falharam pelo caminho ate o servidor desde a ultima
        // resposta que o provou. Ver `kFalhasSeguidasAteParar`.
        unsigned seguidas = 0;
        for (std::size_t i = 0; i < colheita_.n; ++i) {
            // O teto e conferido ENTRE arquivos, nunca no meio de um: abortar
            // um PUT pela metade deixaria o servidor com meio arquivo. O que
            // nao foi tentado continua inteiro no cartao.
            if (esgotou_o_tempo()) {
                const auto faltam = static_cast<unsigned>(colheita_.n - i);
                restantes_ += faltam;
                std::snprintf(msg, sizeof msg,
                              "o tempo da remessa acabou; %u arquivos ficam "
                              "para a proxima", faltam);
                log.warning(kOrigem, msg);
                break;
            }
            const Desfecho d = insiste(cfg, colheita_.nomes[i],
                                       static_cast<unsigned>(i) + 1, total,
                                       log);
            if (prova_o_caminho(d)) { seguidas = 0; }
            if (d == Desfecho::Entregue) { continue; }
            ++falhados_;
            if (aponta_o_caminho(d) || !aponta_o_caminho(motivo)) {
                motivo = d;
            }
            if (!aponta_o_caminho(d)) { continue; }
            // Sem esta parada a remessa so terminaria no teto, dez minutos
            // depois -- inclusive quando o Wi-Fi some depois da primeira
            // entrega. O certificado nao espera o segundo arquivo: sem
            // relogio, aperto de mao nenhum passa.
            ++seguidas;
            const unsigned limite =
                d == Desfecho::Certificado ? 1u : kFalhasSeguidasAteParar;
            if (seguidas < limite) { continue; }
            const auto faltam = static_cast<unsigned>(colheita_.n - i - 1);
            restantes_ += faltam;
            if (faltam > 0) {
                std::snprintf(msg, sizeof msg,
                              "o caminho ate o servidor nao passa; %u "
                              "arquivos ficam para a proxima", faltam);
                log.warning(kOrigem, msg);
            }
            break;
        }
        if (total == 0) {
            saida = ResultadoRemessa::NadaAEnviar;
        } else if (falhados_ > 0 && entregues_ == 0) {
            saida = sem_entrega(motivo);
        } else if (falhados_ > 0) {
            saida = ResultadoRemessa::Parcial;
        } else if (retidos_ > 0) {
            // Antes de "falta mais", de proposito: um cartao que nao solta
            // arquivo e o mais urgente dos dois, porque faz TUDO voltar na
            // remessa seguinte -- e e esse o sintoma que mandou rever isto.
            saida = ResultadoRemessa::NaoApagou;
        } else if (restantes_ > 0) {
            saida = ResultadoRemessa::EnviadaFaltaMais;
        } else {
            saida = ResultadoRemessa::Enviada;
        }
    }

    // Desconectar SEMPRE, inclusive apos falha de cartao: o radio ligado
    // consome e nao serve mais a ninguem.
    rede_.desconecta(log);

    avisa(e_falha(saida) ? FaseRemessa::Falhou : FaseRemessa::Concluida,
          "", 0, 0, 1);
    char fim[160] = {};
    std::snprintf(fim, sizeof fim,
                  "%s (%u entregues, %u retidos, %u falhados, %u para a "
                  "proxima, %lu ms)", descreve(saida), entregues_, retidos_,
                  falhados_, restantes_,
                  static_cast<unsigned long>(pausa_.agora_ms() - inicio_ms_));
    log.info(kOrigem, fim);
    return saida;
}

}  // namespace coruja
