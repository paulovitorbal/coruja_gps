#include "nucleo/GravadorConfig.h"

#include <cstdio>

#include "log/Logger.h"
#include "nucleo/EscritorConfig.h"
#include "nucleo/LeitorConfig.h"

namespace coruja {

namespace {
constexpr const char* kOrigem = "config";
}

ErroCartao le_texto_config(Armazenamento& cartao, char* destino,
                           std::size_t capacidade, std::size_t* lidos,
                           Logger& log) {
    const ErroCartao erro =
        cartao.le_arquivo(kArquivoCfg, destino, capacidade, lidos, log);
    if (erro != ErroCartao::ArquivoAusente) {
        return erro;
    }
    const ErroCartao da_reserva =
        cartao.le_arquivo(kArquivoCfgBak, destino, capacidade, lidos, log);
    if (da_reserva == ErroCartao::Nenhum) {
        // WARN, e nao INFO: e o rastro de uma queda no meio da troca, e o
        // cartao segue sem `coruja.cfg` ate a proxima gravacao de ajuste.
        log.warning(kOrigem, "sem coruja.cfg: operando pela reserva coruja.bak");
        return da_reserva;
    }
    // Sem as duas, quem chama ouve falar do cfg, que e o arquivo que se
    // espera achar. "A reserva tambem nao estava" nao muda o que fazer.
    return erro;
}

bool carrega_configuracao(Armazenamento& cartao, char* buffer,
                          std::size_t capacidade, Configuracao* destino,
                          Logger& log) {
    std::size_t lidos = 0;
    const ErroCartao erro =
        le_texto_config(cartao, buffer, capacidade, &lidos, log);
    if (erro != ErroCartao::Nenhum) {
        char msg[96];
        std::snprintf(msg, sizeof msg, "%s: %s", kArquivoCfg, descreve(erro));
        log.error(kOrigem, msg);
        return false;
    }
    *destino = le_config(buffer, lidos, &log).config;
    return destino->ota_possivel();
}

const char* descreve(ResultadoGravacao r) {
    switch (r) {
        case ResultadoGravacao::Gravado:        return "gravado";
        case ResultadoGravacao::SemCartao:      return "cartao ausente";
        case ResultadoGravacao::ArquivoAusente: return "coruja.cfg nao encontrado";
        case ResultadoGravacao::GrandeDemais:   return "coruja.cfg grande demais";
        case ResultadoGravacao::FalhaDeEscrita: return "falha ao escrever";
        case ResultadoGravacao::FalhaDeTroca:   return "falha na troca atomica";
    }
    return "?";
}

ResultadoGravacao grava_ajustes(Armazenamento& cartao, const Configuracao& cfg,
                                char* trabalho, std::size_t capacidade,
                                Logger& log) {
    // O buffer serve as duas pontas: a primeira metade recebe o arquivo
    // como esta, a segunda a versao reescrita. Precisam coexistir, porque
    // a reescrita le a origem enquanto produz a saida.
    const std::size_t metade = capacidade / 2;
    if (trabalho == nullptr || metade == 0) {
        return ResultadoGravacao::GrandeDemais;
    }
    char* origem = trabalho;
    char* saida = trabalho + metade;

    // Da reserva tambem: depois de uma queda entre os dois renomeios ela e
    // o unico arquivo inteiro, e e gravando que o `coruja.cfg` volta.
    std::size_t lidos = 0;
    const ErroCartao erro =
        le_texto_config(cartao, origem, metade, &lidos, log);
    if (erro == ErroCartao::ArquivoAusente) {
        log.error(kOrigem, "sem coruja.cfg no cartao; ajuste nao gravado");
        return ResultadoGravacao::ArquivoAusente;
    }
    if (erro == ErroCartao::ArquivoGrande) {
        return ResultadoGravacao::GrandeDemais;
    }
    if (erro != ErroCartao::Nenhum) {
        log.error(kOrigem, descreve(erro));
        return ResultadoGravacao::SemCartao;
    }

    const std::size_t n = reescreve_ajustes(origem, lidos, cfg, saida,
                                            capacidade - metade);
    if (n == 0) {
        log.error(kOrigem, "ajustes nao cabem no buffer; nada gravado");
        return ResultadoGravacao::GrandeDemais;
    }

    // Escreve inteiro no temporario e so entao promove. Enquanto o .tmp
    // nao vira .cfg, uma queda de energia nao custa nada.
    if (cartao.grava_arquivo(kArquivoCfgTmp, saida, n, log) !=
        ErroCartao::Nenhum) {
        return ResultadoGravacao::FalhaDeEscrita;
    }
    if (cartao.promove(kArquivoCfgTmp, kArquivoCfg, kArquivoCfgBak, log) !=
        ErroCartao::Nenhum) {
        return ResultadoGravacao::FalhaDeTroca;
    }
    log.info(kOrigem, "ajustes gravados no cartao");
    return ResultadoGravacao::Gravado;
}

}  // namespace coruja
