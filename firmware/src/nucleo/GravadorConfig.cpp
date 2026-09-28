#include "nucleo/GravadorConfig.h"

#include "log/Logger.h"
#include "nucleo/EscritorConfig.h"

namespace coruja {

namespace {
constexpr const char* kOrigem = "config";
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

    std::size_t lidos = 0;
    const ErroCartao erro =
        cartao.le_arquivo(kArquivoCfg, origem, metade, &lidos, log);
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
