#include "rede/AtualizadorOta.h"

#include <cstdio>
#include <cstring>

#include "log/Logger.h"
#include "nucleo/Texto.h"
#include "nucleo/Url.h"

namespace coruja {
namespace {

/// Junta a resposta da consulta de versão num buffer. É uma linha curta, então
/// aqui a acumulação cabe — ao contrário do `radares.bin`.
struct ColetaVersao {
    char*       destino;
    std::size_t capacidade;
    std::size_t usados = 0;
    bool        estourou = false;
};

void ao_receber_versao(void* contexto, const std::uint8_t* bytes,
                       std::size_t tamanho) {
    auto* coleta = static_cast<ColetaVersao*>(contexto);
    for (std::size_t i = 0; i < tamanho; ++i) {
        if (coleta->usados >= coleta->capacidade) {
            coleta->estourou = true;
            return;
        }
        coleta->destino[coleta->usados++] = static_cast<char>(bytes[i]);
    }
}

/// O download tem dois destinos ao mesmo tempo: o verificador e o cartão. Não
/// há etapa intermediária em RAM — 214 KB não cabem ao lado dos 281 KB que a
/// base reserva.
struct DestinoDownload {
    VerificadorDownload* verificador = nullptr;
    Armazenamento*       cartao = nullptr;
    ObservadorOta*       observador = nullptr;
    bool                 falhou_a_escrita = false;
};

void ao_receber_base(void* contexto, const std::uint8_t* bytes,
                     std::size_t tamanho) {
    auto* destino = static_cast<DestinoDownload*>(contexto);
    destino->verificador->alimenta(bytes, tamanho);

    // Depois da primeira falha de escrita, para de tentar: insistir encheria o
    // log com uma linha por pacote e não mudaria o desfecho.
    if (!destino->falhou_a_escrita &&
        !destino->cartao->escreve(bytes, tamanho)) {
        destino->falhou_a_escrita = true;
    }

    if (destino->observador != nullptr) {
        // O total so existe depois do cabecalho: e ele que diz quantos
        // pontos vem. Antes disso vai zero, e quem desenha trata.
        //
        // Avisa a CADA pedaco, sem limitar a taxa aqui: quem desenha ja
        // redesenha so quando o pixel da barra muda, e duplicar essa
        // decisao seria dois lugares para ajustar o mesmo compromisso.
        const auto& v = *destino->verificador;
        const std::size_t total =
            v.tem_cabecalho()
                ? kTamCabecalho + static_cast<std::size_t>(
                                      v.cabecalho().n_pontos) * kTamRegistro
                : 0;
        destino->observador->progresso(v.bytes_recebidos(), total);
    }
}

void imprime_cabecalho(const VerificadorDownload& v, Logger& log) {
    char msg[110];
    const CabecalhoBase& c = v.cabecalho();

    log.info("base", "---- cabecalho do radares.bin recebido ----");

    char magic[5] = {};
    std::memcpy(magic, &c.magic, 4);
    for (int i = 0; i < 4; ++i) {
        if (magic[i] < 32 || magic[i] > 126) {
            magic[i] = '?';
        }
    }
    std::snprintf(msg, sizeof msg, "  magic        : '%s' (0x%08lX)%s", magic,
                  static_cast<unsigned long>(c.magic),
                  c.magic == kMagic ? "" : "  <- ESPERADO 'RDR1'");
    log.info("base", msg);

    std::snprintf(msg, sizeof msg, "  versao       : %u%s",
                  static_cast<unsigned>(c.versao),
                  c.versao == kVersao ? "" : "  <- INCOMPATIVEL");
    log.info("base", msg);
    std::snprintf(msg, sizeof msg,
                  "  exp_escala   : %u  (coordenadas x10^%u)",
                  static_cast<unsigned>(c.exp_escala),
                  static_cast<unsigned>(c.exp_escala));
    log.info("base", msg);
    std::snprintf(msg, sizeof msg, "  tam_registro : %u B",
                  static_cast<unsigned>(c.tam_registro));
    log.info("base", msg);
    std::snprintf(msg, sizeof msg, "  n_pontos     : %lu  (declarados)",
                  static_cast<unsigned long>(c.n_pontos));
    log.info("base", msg);
    std::snprintf(msg, sizeof msg, "  crc32        : 0x%08lX  (do cabecalho)",
                  static_cast<unsigned long>(c.crc));
    log.info("base", msg);

    log.info("base", "---- e o que de fato chegou ----");
    std::snprintf(msg, sizeof msg, "  bytes        : %lu  (%lu de dados)",
                  static_cast<unsigned long>(v.bytes_recebidos()),
                  static_cast<unsigned long>(v.bytes_de_dados()));
    log.info("base", msg);
    std::snprintf(msg, sizeof msg, "  pontos       : %lu%s",
                  static_cast<unsigned long>(v.pontos_recebidos()),
                  v.pontos_recebidos() == c.n_pontos ? "  (confere)"
                                                     : "  <- DIVERGE");
    log.info("base", msg);
    std::snprintf(msg, sizeof msg, "  crc32        : 0x%08lX%s",
                  static_cast<unsigned long>(v.crc_calculado()),
                  v.crc_calculado() == c.crc ? "  (confere)" : "  <- DIVERGE");
    log.info("base", msg);
    log.info("base", "------------------------------------------");
}

}  // namespace

const char* descreve(FaseOta fase) {
    // Texto curto de proposito: vai para a faixa de 26 caracteres da tela,
    // e uma frase mais longa rolaria em vez de ser lida de relance.
    switch (fase) {
        case FaseOta::Conectando:  return "CONECTANDO";
        case FaseOta::Consultando: return "CONSULTANDO VERSAO";
        case FaseOta::JaEmDia:     return "JA ESTA EM DIA";
        case FaseOta::Baixando:    return "BAIXANDO";
        case FaseOta::Verificando: return "VERIFICANDO";
        case FaseOta::Gravando:    return "GRAVANDO";
        case FaseOta::Concluida:   return "ATUALIZADA";
        case FaseOta::Falhou:      return "FALHOU";
    }
    return "?";
}

const char* descreve(ResultadoOta resultado) {
    switch (resultado) {
        case ResultadoOta::Atualizada:       return "base atualizada";
        case ResultadoOta::JaEstavaEmDia:    return "ja estava na ultima versao";
        case ResultadoOta::SemConfiguracao:  return "sem rede ou sem URLs configuradas";
        case ResultadoOta::FalhaDeRede:      return "nao conseguiu conectar";
        case ResultadoOta::FalhaAoConsultar: return "nao conseguiu consultar a versao";
        case ResultadoOta::FalhaAoBaixar:    return "o download nao completou";
        case ResultadoOta::BaseRecusada:     return "base recusada na verificacao";
        case ResultadoOta::FalhaAoGravar:    return "chegou integra, mas o cartao nao aceitou";
    }
    return "resultado desconhecido";
}

const char* descreve_curto(ResultadoOta resultado) {
    // Maiusculas e sem acento, como o resto da tela: a fonte de texto do
    // §4.1 e um subconjunto gerado, e acento nao esta nele.
    switch (resultado) {
        case ResultadoOta::Atualizada:       return "ATUALIZADA";
        case ResultadoOta::JaEstavaEmDia:    return "JA ESTA EM DIA";
        case ResultadoOta::SemConfiguracao:  return "SEM CONFIGURACAO";
        case ResultadoOta::FalhaDeRede:      return "SEM WI-FI";
        case ResultadoOta::FalhaAoConsultar: return "SERVIDOR NAO RESPONDE";
        case ResultadoOta::FalhaAoBaixar:    return "DOWNLOAD INCOMPLETO";
        case ResultadoOta::BaseRecusada:     return "ARQUIVO CORROMPIDO";
        case ResultadoOta::FalhaAoGravar:    return "CARTAO NAO GRAVOU";
    }
    return "ERRO DESCONHECIDO";
}

ResultadoOta AtualizadorOta::executa(const Configuracao& cfg, Logger& log) {
    // Apelidos locais: o corpo abaixo foi escrito contra parametros e nao
    // muda ao passarem a ser membros. Trocar os nomes seria diff sem ganho.
    Armazenamento& cartao = cartao_;
    Conexao&       rede = rede_;
    char msg[224];

    if (!cfg.ota_possivel()) {
        log.warning("ota", "sem rede ou sem URLs: nada a fazer");
        return ResultadoOta::SemConfiguracao;
    }

    Url url_versao;
    Url url_base;
    if (analisa_url(cfg.url_versao, &url_versao) != ErroUrl::Nenhum ||
        analisa_url(cfg.url_base, &url_base) != ErroUrl::Nenhum) {
        log.error("ota", "URL da configuracao nao e analisavel");
        return ResultadoOta::SemConfiguracao;
    }

    // O segredo do aparelho NAO entra na URL: ele vai no cabecalho
    // `X-Coruja-Token`, que o `ClienteTls` acrescenta a toda requisicao.
    //
    // Houve uma versao em que ele vinha aqui, como `?t=`, porque o
    // `http_client` do lwIP montava o pedido internamente e nao tinha onde
    // encaixar cabecalho proprio. Com TLS esse transporte foi reescrito, o
    // gancho passou a existir, e o segredo saiu da URL -- onde entrava no log
    // de acesso do servidor e de qualquer proxy no caminho.

    log.info("ota", "==== atualizacao solicitada ====");

    // A versão local vem do CARTÃO. Guardá-la só em RAM fazia o primeiro
    // clique depois de cada boot rebaixar 214 KB sem necessidade.
    std::size_t lidos = 0;
    versao_local_[0] = '\0';
    const auto leitura_versao = cartao.le_arquivo(kArquivoVersao, versao_local_,
                                                  kMaxVersao, &lidos, log);
    if (leitura_versao == ErroCartao::Nenhum) {
        apara_branco(versao_local_, &lidos);
    } else if (leitura_versao == ErroCartao::ArquivoAusente) {
        // Normal na primeira atualização. Não é erro, e não deve soar como um.
        log.info("ota", "sem versao registrada no cartao: primeira atualizacao");
    } else {
        std::snprintf(msg, sizeof msg, "versao local ilegivel (%s): vai rebaixar",
                      descreve(leitura_versao));
        log.warning("ota", msg);
    }

    const auto avisa = [&](FaseOta f, unsigned tentativa = 1) {
        if (observador_ != nullptr) {
            observador_->fase(f, tentativa);
        }
    };

    avisa(FaseOta::Conectando);
    const auto erro_rede = rede.conecta(cfg, log);
    if (erro_rede != ErroWifi::Nenhum) {
        std::snprintf(msg, sizeof msg, "rede: %s", descreve(erro_rede));
        log.error("ota", msg);
        avisa(FaseOta::Falhou);
        return ResultadoOta::FalhaDeRede;
    }

    // AGORA, e nao antes: o `conecta()` acima e quem chama o
    // `cyw43_arch_init()`. Qualquer uso da pilha de rede antes deste ponto
    // fala com um radio que ainda nao existe -- e o aparelho trava por
    // completo, sem erro. Ver `PreparoDeSessao`.
    if (preparo_ != nullptr) {
        preparo_->apos_conectar(log);
    }

    // `desconecta` tem de acontecer em TODA saída daqui para baixo. Um retorno
    // adiantado que esquecesse disso deixaria o rádio associado indefinidamente
    // — exatamente o que a conexão episódica existe para evitar.
    const auto encerra = [&](ResultadoOta r) {
        // A fase final sai daqui, e nao de cada ponto de retorno: e o unico
        // lugar por onde todos passam, e foi escrito justamente para que o
        // `desconecta` nao fosse esquecido em nenhum deles.
        avisa(r == ResultadoOta::Atualizada      ? FaseOta::Concluida
              : r == ResultadoOta::JaEstavaEmDia ? FaseOta::JaEmDia
                                                 : FaseOta::Falhou);
        std::snprintf(msg, sizeof msg, "resultado: %s", descreve(r));
        log.info("ota", msg);
        rede.desconecta(log);
        log.info("ota", "==== fim da atualizacao ====");
        return r;
    };

    // ---- 1. consultar a versão disponível --------------------------------
    avisa(FaseOta::Consultando);
    ColetaVersao coleta{versao_remota_, kMaxVersao};
    const auto consulta = http_.baixa(url_versao, ao_receber_versao, &coleta,
                                      log, 15'000);
    if (!consulta.ok()) {
        std::snprintf(msg, sizeof msg, "consulta de versao: %s",
                      descreve(consulta.erro));
        log.error("ota", msg);
        return encerra(ResultadoOta::FalhaAoConsultar);
    }
    if (coleta.estourou) {
        log.warning("ota", "resposta de versao maior que o buffer; truncada");
    }
    versao_remota_tam_ = coleta.usados;
    apara_branco(versao_remota_, &versao_remota_tam_);
    if (versao_remota_tam_ == 0) {
        log.error("ota", "o servidor devolveu versao vazia");
        return encerra(ResultadoOta::FalhaAoConsultar);
    }

    std::snprintf(msg, sizeof msg, "versao no servidor: '%s'", versao_remota_);
    log.info("ota", msg);
    std::snprintf(msg, sizeof msg, "versao local      : '%s'",
                  versao_local_[0] != '\0' ? versao_local_ : "(nenhuma)");
    log.info("ota", msg);

    if (std::strcmp(versao_local_, versao_remota_) == 0) {
        return encerra(ResultadoOta::JaEstavaEmDia);
    }

    // ---- 2. baixar, gravando e verificando ao mesmo tempo ----------------
    log.info("ota", "versao diferente: baixando a base");

    ResultadoOta ultimo_erro = ResultadoOta::FalhaAoBaixar;
    for (unsigned tentativa = 1; tentativa <= kTentativas; ++tentativa) {
        if (tentativa > 1) {
            std::snprintf(msg, sizeof msg, "tentativa %u de %u, apos %lu ms",
                          tentativa, kTentativas,
                          static_cast<unsigned long>(kEsperaEntreTentativasMs));
            log.warning("ota", msg);
            pausa_.espera_ms(kEsperaEntreTentativasMs);
        }

        avisa(FaseOta::Baixando, tentativa);
        verificador_.reinicia();
        const auto abertura = cartao.abre_para_escrita(kArquivoTmp, log);
        if (abertura != ErroCartao::Nenhum) {
            std::snprintf(msg, sizeof msg, "nao deu para abrir '%s': %s",
                          kArquivoTmp, descreve(abertura));
            log.error("ota", msg);
            ultimo_erro = ResultadoOta::FalhaAoGravar;
            continue;
        }

        DestinoDownload destino;
        destino.verificador = &verificador_;
        destino.cartao = &cartao;
        destino.observador = observador_;

        const auto download = http_.baixa(url_base, ao_receber_base, &destino,
                                          log, 120'000);

        if (!download.ok()) {
            std::snprintf(msg, sizeof msg, "download: %s (%lu B chegaram)",
                          descreve(download.erro),
                          static_cast<unsigned long>(verificador_.bytes_recebidos()));
            log.error("ota", msg);
            cartao.descarta_escrita(kArquivoTmp, log);
            ultimo_erro = ResultadoOta::FalhaAoBaixar;
            continue;
        }
        if (destino.falhou_a_escrita) {
            log.error("ota", "o cartao recusou a escrita no meio do download");
            cartao.descarta_escrita(kArquivoTmp, log);
            ultimo_erro = ResultadoOta::FalhaAoGravar;
            continue;
        }

        avisa(FaseOta::Verificando, tentativa);
        imprime_cabecalho(verificador_, log);

        // Passo 2 do RF05.2. Valida ANTES de mexer na base vigente: é o que
        // garante que um arquivo remoto corrompido não derrube a que funciona.
        const auto veredito = verificador_.conclui(kCapacidadeFirmware);
        if (veredito != ErroBase::Nenhum) {
            std::snprintf(msg, sizeof msg, "base RECUSADA: %s", descreve(veredito));
            log.error("ota", msg);
            cartao.descarta_escrita(kArquivoTmp, log);
            ultimo_erro = ResultadoOta::BaseRecusada;
            continue;
        }

        if (cartao.conclui_escrita(log) != ErroCartao::Nenhum) {
            cartao.descarta_escrita(kArquivoTmp, log);
            ultimo_erro = ResultadoOta::FalhaAoGravar;
            continue;
        }

        // ---- 3 e 4. a troca atômica --------------------------------------
        avisa(FaseOta::Gravando, tentativa);
        const auto troca = cartao.promove(kArquivoTmp, kArquivoBase,
                                          kArquivoBak, log);
        if (troca != ErroCartao::Nenhum) {
            std::snprintf(msg, sizeof msg, "troca atomica: %s", descreve(troca));
            log.error("ota", msg);
            return encerra(ResultadoOta::FalhaAoGravar);
        }

        // ---- 5. a versão, por último -------------------------------------
        //
        // Depois da base estar no lugar, de propósito. Se o aparelho desligar
        // entre as duas, sobra a versão antiga e o próximo clique rebaixa:
        // desperdício de rede, que é o modo de falha certo para escolher.
        std::memcpy(versao_local_, versao_remota_, versao_remota_tam_ + 1);
        const auto gravou_versao = cartao.grava_arquivo(
            kArquivoVersao, versao_remota_, versao_remota_tam_, log);
        if (gravou_versao != ErroCartao::Nenhum) {
            log.warning("ota", "base gravada, mas a versao nao: vai rebaixar "
                               "no proximo clique");
        }

        std::snprintf(msg, sizeof msg, "base gravada em '%s' (%lu bytes)",
                      kArquivoBase,
                      static_cast<unsigned long>(verificador_.bytes_recebidos()));
        log.info("ota", msg);
        return encerra(ResultadoOta::Atualizada);
    }

    log.error("ota", "as tentativas se esgotaram; a base vigente segue intacta");
    return encerra(ultimo_erro);
}

}  // namespace coruja
