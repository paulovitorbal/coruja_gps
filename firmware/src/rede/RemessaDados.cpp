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

void RemessaDados::avisa(FaseRemessa fase, const char* nome, unsigned indice,
                         unsigned total, unsigned tentativa) {
    if (observador_ != nullptr) {
        observador_->fase(fase, nome, indice, total, tentativa);
    }
}

RemessaDados::Desfecho RemessaDados::classifica(const ResultadoHttp& r) {
    if (r.erro == ErroHttp::CertificadoRecusado) { return Desfecho::Certificado; }
    // O cliente nem montou o pedido: host vazio, ou cabecalho que nao coube.
    // E configuracao, como a URL que nao forma.
    if (r.erro == ErroHttp::UrlInvalida) { return Desfecho::Url; }
    // Vale o STATUS, e nao o erro. O servidor recusa sem ler o corpo e fecha
    // (`_descarta_corpo`), e o RST pode chegar logo atras da resposta: o
    // cliente devolve `Interrompida`, mas com o 401 que chegou a ler.
    //
    // Abaixo de 300 nao houve resposta que servisse: nenhuma linha de status,
    // ou uma que caiu antes de terminar, ou que veio sem o CRC.
    if (r.status < 300) { return Desfecho::SemResposta; }
    if (r.status == 413) { return Desfecho::GrandeDemais; }
    // Os dois 4xx que pedem, eles mesmos, outra tentativa.
    if (r.status == 408 || r.status == 429) { return Desfecho::ErroNoServidor; }
    // 3xx junto com 4xx: o aparelho nao segue redirecionamento, e o 301 de um
    // `http://` que o proxy manda para `https://` volta a cada tentativa.
    if (r.status < 500) { return Desfecho::Recusado; }
    return Desfecho::ErroNoServidor;
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
        }
        avisa(FaseRemessa::Confirmando, nome, indice, total, tentativa);

        std::uint32_t crc_remoto = 0;
        const auto consulta = http_.consulta_crc(url, cfg.token_aparelho,
                                                 &crc_remoto, log,
                                                 kTempoLimiteConsultaMs);
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
    const auto erro_rede = rede_.conecta(cfg, log);
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
        // A falha mais recente. Quando nada passou, e ela que diz por que --
        // e o que a tela diz ao usuario.
        Desfecho ultima_falha = Desfecho::Entregue;
        // Arquivos que falharam pelo caminho ate o servidor desde a ultima
        // entrega. Ver `kFalhasSeguidasAteParar`.
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
            if (d == Desfecho::Entregue) {
                seguidas = 0;
                continue;
            }
            ++falhados_;
            ultima_falha = d;
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
            saida = sem_entrega(ultima_falha);
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
