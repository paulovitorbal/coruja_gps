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

void ao_receber_base(void* contexto, const std::uint8_t* bytes,
                     std::size_t tamanho) {
    static_cast<VerificadorDownload*>(contexto)->alimenta(bytes, tamanho);
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

const char* descreve(ResultadoOta resultado) {
    switch (resultado) {
        case ResultadoOta::Atualizada:       return "base atualizada";
        case ResultadoOta::JaEstavaEmDia:    return "ja estava na ultima versao";
        case ResultadoOta::SemConfiguracao:  return "sem rede ou sem URLs configuradas";
        case ResultadoOta::FalhaDeRede:      return "nao conseguiu conectar";
        case ResultadoOta::FalhaAoConsultar: return "nao conseguiu consultar a versao";
        case ResultadoOta::FalhaAoBaixar:    return "o download nao completou";
        case ResultadoOta::BaseRecusada:     return "base recusada na verificacao";
    }
    return "resultado desconhecido";
}

ResultadoOta AtualizadorOta::executa(const Configuracao& cfg, RedeWifi& rede,
                                     Logger& log) {
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

    log.info("ota", "==== atualizacao solicitada ====");

    const auto erro_rede = rede.conecta(cfg, log);
    if (erro_rede != ErroWifi::Nenhum) {
        std::snprintf(msg, sizeof msg, "rede: %s", descreve(erro_rede));
        log.error("ota", msg);
        return ResultadoOta::FalhaDeRede;
    }

    // `desconecta` tem de acontecer em TODA saída daqui para baixo. Um retorno
    // adiantado que esquecesse disso deixaria o rádio associado indefinidamente
    // — exatamente o que a conexão episódica existe para evitar.
    const auto encerra = [&](ResultadoOta r) {
        std::snprintf(msg, sizeof msg, "resultado: %s", descreve(r));
        log.info("ota", msg);
        rede.desconecta(log);
        log.info("ota", "==== fim da atualizacao ====");
        return r;
    };

    // ---- 1. consultar a versão disponível --------------------------------
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

    // ---- 2. baixar e verificar em fluxo ----------------------------------
    log.info("ota", "versao diferente: baixando a base");
    verificador_.reinicia();
    const auto download = http_.baixa(url_base, ao_receber_base, &verificador_,
                                      log, 120'000);
    if (!download.ok()) {
        std::snprintf(msg, sizeof msg, "download: %s (%lu B chegaram)",
                      descreve(download.erro),
                      static_cast<unsigned long>(verificador_.bytes_recebidos()));
        log.error("ota", msg);
        return encerra(ResultadoOta::FalhaAoBaixar);
    }

    imprime_cabecalho(verificador_, log);

    const auto veredito = verificador_.conclui(kCapacidadeFirmware);
    if (veredito != ErroBase::Nenhum) {
        std::snprintf(msg, sizeof msg, "base RECUSADA: %s", descreve(veredito));
        log.error("ota", msg);
        return encerra(ResultadoOta::BaseRecusada);
    }

    std::memcpy(versao_local_, versao_remota_, versao_remota_tam_ + 1);
    log.info("ota", "base aceita e verificada");
    log.warning("ota",
                "nao foi gravada: o leitor de cartao ainda nao existe (R-38)");
    return encerra(ResultadoOta::Atualizada);
}

}  // namespace coruja
