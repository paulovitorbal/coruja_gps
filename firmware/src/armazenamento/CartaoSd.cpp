#include "armazenamento/CartaoSd.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include "f_util.h"
#include "ff.h"
#include "hw_config.h"
}

#include "log/Logger.h"

// Se o nosso ffconf.h perder a disputa de caminho de include, a sondagem de
// partições deixa de existir — silenciosamente, porque o código compila e só
// enxerga a primeira partição. O R-38 volta a valer sem que nada avise. Aqui
// o build quebra.
static_assert(FF_MULTI_PARTITION == 1,
              "o ffconf.h do Coruja nao foi usado: a sondagem de particoes "
              "(R-38) nao existiria. Confira a ordem dos include no CMake.");
static_assert(FF_VOLUMES >= 5, "faltam volumes para sondar as 4 particoes");

/// Mapa de volume para partição, exigido pelo FatFs quando
/// `FF_MULTI_PARTITION == 1`.
///
/// Volume 0 é **detecção automática**: cobre cartão de uma partição e cartão
/// sem MBR, que é o caso comum e o que se tenta primeiro. Os volumes 1 a 4 são
/// as quatro entradas primárias da MBR, forçadas — é assim que se consegue
/// olhar a segunda partição, que o modo automático nunca alcançaria.
///
/// Partição lógica dentro de estendida continua fora do alcance: a MBR só tem
/// quatro entradas primárias, e o FatFs não percorre a cadeia de estendidas.
extern "C" PARTITION VolToPart[FF_VOLUMES] = {
    {0, 0},  // "0:" — automático
    {0, 1},  // "1:" — 1ª partição primária
    {0, 2},  // "2:" — 2ª
    {0, 3},  // "3:" — 3ª
    {0, 4},  // "4:" — 4ª
};

namespace coruja {
namespace {

constexpr int kQuantosVolumes = 5;

/// Um objeto só, reaproveitado entre as tentativas. Cada `FATFS` carrega um
/// setor de 512 B, e cinco deles custariam 2,5 KiB para nada: as tentativas
/// são sequenciais e só uma fica montada por vez.
FATFS g_fs;
FIL   g_arquivo;

}  // namespace

const char* descreve(ErroCartao erro) {
    switch (erro) {
        case ErroCartao::Nenhum:         return "ok";
        case ErroCartao::SemCartaoLegivel:
            return "cartao ausente ou ilegivel";
        case ErroCartao::ArquivoAusente: return "arquivo nao encontrado em nenhuma particao";
        case ErroCartao::ArquivoGrande:  return "arquivo maior que o buffer";
        case ErroCartao::FalhaDeLeitura: return "falha de leitura";
        case ErroCartao::FalhaDeEscrita: return "falha de escrita";
        case ErroCartao::FalhaDeRenomeacao: return "falha ao renomear (troca atomica)";
    }
    return "erro desconhecido";
}

void CartaoSd::inicia(Logger& log) {
    if (iniciado_) {
        return;
    }
    // `sd_init_driver()` configura o SPI e o pino de card detect. É idempotente
    // na biblioteca, mas guardamos a chamada mesmo assim.
    if (!sd_init_driver()) {
        log.error("sd", "sd_init_driver falhou");
        return;
    }
    iniciado_ = true;
    log.info("sd", "driver do cartao iniciado");
}

ErroCartao CartaoSd::le_arquivo(const char* nome, char* destino,
                                std::size_t capacidade, std::size_t* lidos,
                                Logger& log) {
    if (lidos != nullptr) {
        *lidos = 0;
    }
    if (!iniciado_) {
        inicia(log);
    }
    char msg[128];
    int  montadas = 0;

    // Sonda as partições **procurando o arquivo**, não a primeira que for FAT.
    // É a decisão do R-38: num cartão de duas partições, escolher pela
    // primeira FAT daria "sem configuracao" com o arquivo no cartão.
    for (int volume = 0; volume < kQuantosVolumes; ++volume) {
        char raiz[4];
        std::snprintf(raiz, sizeof raiz, "%d:", volume);

        const FRESULT montagem = f_mount(&g_fs, raiz, 1);
        if (montagem != FR_OK) {
            // A causa PRECISA sair no log. A primeira versão fazia `continue`
            // em silêncio, e o resultado era "nenhuma particao FAT montou" —
            // verdadeiro, inútil e indistinguível entre cartão mal fiado,
            // cartão não-FAT e cartão ausente.
            //
            // `FR_NOT_READY` aponta para o barramento ou a alimentação;
            // `FR_NO_FILESYSTEM`, para a formatação; `FR_DISK_ERR`, para
            // leitura de setor. São três problemas com três soluções.
            std::snprintf(msg, sizeof msg, "volume %d (%s) nao montou: %s",
                          volume, volume == 0 ? "automatico" : "particao forcada",
                          FRESULT_str(montagem));
            log.info("sd", msg);
            continue;
        }
        ++montadas;

        char caminho[64];
        std::snprintf(caminho, sizeof caminho, "%d:/%s", volume, nome);

        const FRESULT abertura = f_open(&g_arquivo, caminho, FA_READ);
        if (abertura != FR_OK) {
            std::snprintf(msg, sizeof msg,
                          "volume %d montou (%s), sem '%s'", volume,
                          volume == 0 ? "automatico" : "particao forcada", nome);
            log.debug("sd", msg);
            f_unmount(raiz);
            continue;
        }

        const FSIZE_t tamanho = f_size(&g_arquivo);
        std::snprintf(msg, sizeof msg, "'%s' achado no volume %d, %lu bytes",
                      nome, volume, static_cast<unsigned long>(tamanho));
        log.info("sd", msg);

        if (tamanho >= capacidade) {
            f_close(&g_arquivo);
            f_unmount(raiz);
            return ErroCartao::ArquivoGrande;
        }

        UINT          obtidos = 0;
        const FRESULT leitura = f_read(&g_arquivo, destino,
                                       static_cast<UINT>(tamanho), &obtidos);
        f_close(&g_arquivo);
        f_unmount(raiz);

        if (leitura != FR_OK || obtidos != tamanho) {
            std::snprintf(msg, sizeof msg, "f_read: %s (%u de %lu bytes)",
                          FRESULT_str(leitura), static_cast<unsigned>(obtidos),
                          static_cast<unsigned long>(tamanho));
            log.error("sd", msg);
            return ErroCartao::FalhaDeLeitura;
        }

        destino[obtidos] = '\0';
        if (lidos != nullptr) {
            *lidos = obtidos;
        }
        // A base tem de acompanhar a configuração que a definiu: guarda o
        // volume para que a escrita vá ao mesmo lugar, sem sondar de novo.
        volume_ativo_ = volume;
        return ErroCartao::Nenhum;
    }

    if (montadas == 0) {
        // Sem pino de card detect, ausente e ilegível chegam aqui iguais — e
        // é assim que o projeto quer (ADR 0010).
        log.error("sd", "nenhuma particao FAT montou: cartao ausente ou ilegivel");
        return ErroCartao::SemCartaoLegivel;
    }
    // A mensagem diz QUANTAS partições foram vistas. É o que separa um beco
    // sem saída de um diagnóstico: com o arquivo na partição errada, este
    // número é a pista.
    //
    // ⚠️ Registrado como **informação**, não erro. Arquivo ausente nem sempre
    // é problema: na primeira atualização o `versao.txt` não existe, e isso é
    // o estado normal. Quem sabe se o arquivo era obrigatório é quem chamou —
    // e é lá que a severidade pertence.
    std::snprintf(msg, sizeof msg,
                  "'%s' nao esta em nenhuma das %d particoes FAT montadas",
                  nome, montadas);
    log.info("sd", msg);
    return ErroCartao::ArquivoAusente;
}


ErroCartao CartaoSd::le_em_fluxo(const char* nome, AoLerPedaco ao_ler,
                                 void* contexto, std::size_t* lidos,
                                 Logger& log) {
    // 4 KiB: oito setores por transferência, e o bastante para o custo por
    // chamada sumir perto do tempo de leitura.
    static std::uint8_t pedaco[4096];

    if (lidos != nullptr) {
        *lidos = 0;
    }
    char raiz[4];
    const auto erro = monta_volume(raiz, sizeof raiz, log);
    if (erro != ErroCartao::Nenhum) {
        return erro;
    }

    char caminho[64], msg[128];
    std::snprintf(caminho, sizeof caminho, "%s/%s", raiz, nome);
    const FRESULT abertura = f_open(&g_arquivo, caminho, FA_READ);
    if (abertura != FR_OK) {
        f_unmount(raiz);
        return ErroCartao::ArquivoAusente;
    }

    const FSIZE_t tamanho = f_size(&g_arquivo);
    std::snprintf(msg, sizeof msg, "lendo '%s' em fluxo: %lu bytes", nome,
                  static_cast<unsigned long>(tamanho));
    log.info("sd", msg);

    ErroCartao saida = ErroCartao::Nenhum;
    std::size_t total = 0;
    while (true) {
        UINT obtidos = 0;
        const FRESULT r = f_read(&g_arquivo, pedaco, sizeof pedaco, &obtidos);
        if (r != FR_OK) {
            std::snprintf(msg, sizeof msg, "f_read em '%s': %s", nome,
                          FRESULT_str(r));
            log.error("sd", msg);
            saida = ErroCartao::FalhaDeLeitura;
            break;
        }
        if (obtidos == 0) {
            break;  // fim do arquivo
        }
        total += obtidos;
        if (ao_ler != nullptr) {
            ao_ler(contexto, pedaco, obtidos);
        }
    }

    f_close(&g_arquivo);
    f_unmount(raiz);
    if (lidos != nullptr) {
        *lidos = total;
    }
    return saida;
}

// ---------------------------------------------------------------- escrita ---

ErroCartao CartaoSd::monta_volume(char* raiz, std::size_t tam_raiz, Logger& log) {
    if (!iniciado_) {
        inicia(log);
    }
    // Volume conhecido: vai direto. Desconhecido: sonda, como a leitura faz.
    const int primeiro = volume_ativo_ >= 0 ? volume_ativo_ : 0;
    const int ultimo   = volume_ativo_ >= 0 ? volume_ativo_ : kQuantosVolumes - 1;

    char msg[96];
    for (int volume = primeiro; volume <= ultimo; ++volume) {
        std::snprintf(raiz, tam_raiz, "%d:", volume);
        const FRESULT montagem = f_mount(&g_fs, raiz, 1);
        if (montagem == FR_OK) {
            volume_ativo_ = volume;
            return ErroCartao::Nenhum;
        }
        std::snprintf(msg, sizeof msg, "volume %d nao montou para escrita: %s",
                      volume, FRESULT_str(montagem));
        log.debug("sd", msg);
    }
    log.error("sd", "nenhum volume montou para escrita");
    return ErroCartao::SemCartaoLegivel;
}

ErroCartao CartaoSd::abre_para_escrita(const char* nome, Logger& log) {
    if (escrevendo_) {
        log.error("sd", "abre_para_escrita com uma escrita ja em curso");
        return ErroCartao::FalhaDeEscrita;
    }
    const auto erro = monta_volume(raiz_aberta_, sizeof raiz_aberta_, log);
    if (erro != ErroCartao::Nenhum) {
        return erro;
    }

    char caminho[64];
    std::snprintf(caminho, sizeof caminho, "%s/%s", raiz_aberta_, nome);
    const FRESULT r = f_open(&g_arquivo, caminho, FA_WRITE | FA_CREATE_ALWAYS);
    if (r != FR_OK) {
        char msg[96];
        std::snprintf(msg, sizeof msg, "f_open('%s') para escrita: %s", caminho,
                      FRESULT_str(r));
        log.error("sd", msg);
        f_unmount(raiz_aberta_);
        return ErroCartao::FalhaDeEscrita;
    }
    escrevendo_ = true;
    return ErroCartao::Nenhum;
}

bool CartaoSd::escreve(const std::uint8_t* bytes, std::size_t tamanho) {
    if (!escrevendo_ || bytes == nullptr) {
        return false;
    }
    UINT gravados = 0;
    const FRESULT r = f_write(&g_arquivo, bytes, static_cast<UINT>(tamanho),
                              &gravados);
    // Gravação curta conta como falha: cartão cheio devolve FR_OK com menos
    // bytes, e aceitar isso produziria um arquivo truncado que parece bom.
    return r == FR_OK && gravados == tamanho;
}

ErroCartao CartaoSd::conclui_escrita(Logger& log) {
    if (!escrevendo_) {
        return ErroCartao::FalhaDeEscrita;
    }
    const FRESULT r = f_close(&g_arquivo);
    escrevendo_ = false;
    f_unmount(raiz_aberta_);
    if (r != FR_OK) {
        char msg[64];
        std::snprintf(msg, sizeof msg, "f_close: %s", FRESULT_str(r));
        log.error("sd", msg);
        return ErroCartao::FalhaDeEscrita;
    }
    return ErroCartao::Nenhum;
}

void CartaoSd::descarta_escrita(const char* nome, Logger& log) {
    if (!escrevendo_) {
        return;
    }
    f_close(&g_arquivo);
    escrevendo_ = false;

    char caminho[64];
    std::snprintf(caminho, sizeof caminho, "%s/%s", raiz_aberta_, nome);
    const FRESULT r = f_unlink(caminho);
    f_unmount(raiz_aberta_);

    char msg[96];
    std::snprintf(msg, sizeof msg, "'%s' descartado%s", nome,
                  r == FR_OK ? "" : " (nao deu para apagar)");
    log.warning("sd", msg);
}

ErroCartao CartaoSd::promove(const char* temporario, const char* base,
                             const char* reserva, Logger& log) {
    char raiz[4];
    const auto erro = monta_volume(raiz, sizeof raiz, log);
    if (erro != ErroCartao::Nenhum) {
        return erro;
    }

    char p_tmp[64], p_base[64], p_bak[64], msg[128];
    std::snprintf(p_tmp,  sizeof p_tmp,  "%s/%s", raiz, temporario);
    std::snprintf(p_base, sizeof p_base, "%s/%s", raiz, base);
    std::snprintf(p_bak,  sizeof p_bak,  "%s/%s", raiz, reserva);

    // Passo 3 do RF05.2. Só faz sentido se já houver base; na primeira
    // atualização não há, e a ausência não é erro.
    FILINFO info;
    if (f_stat(p_base, &info) == FR_OK) {
        f_unlink(p_bak);  // um .bak antigo impediria o rename
        const FRESULT r = f_rename(p_base, p_bak);
        if (r != FR_OK) {
            std::snprintf(msg, sizeof msg, "nao deu para guardar a base atual "
                          "como '%s': %s", reserva, FRESULT_str(r));
            log.error("sd", msg);
            f_unmount(raiz);
            return ErroCartao::FalhaDeRenomeacao;
        }
        std::snprintf(msg, sizeof msg, "base anterior guardada como '%s'", reserva);
        log.info("sd", msg);
    }

    // Passo 4. Deste ponto em diante o cartão volta a estar consistente.
    const FRESULT r = f_rename(p_tmp, p_base);
    f_unmount(raiz);
    if (r != FR_OK) {
        std::snprintf(msg, sizeof msg, "nao deu para promover '%s' a '%s': %s",
                      temporario, base, FRESULT_str(r));
        log.error("sd", msg);
        // A base vigente virou .bak e a nova não entrou. Quem chama precisa
        // saber: este é o único instante em que o cartão fica sem `base`.
        return ErroCartao::FalhaDeRenomeacao;
    }
    return ErroCartao::Nenhum;
}

ErroCartao CartaoSd::acrescenta_arquivo(const char* nome, const char* conteudo,
                                        std::size_t tamanho, Logger& log) {
    if (escrevendo_) {
        // Uma escrita em fluxo está em curso — tipicamente o download da base.
        // Acrescentar log no meio dela usaria o mesmo `FIL` e corromperia os
        // dois arquivos. Silenciosamente pular é o certo aqui: perder linhas
        // de log é muito melhor que perder a base.
        return ErroCartao::FalhaDeEscrita;
    }
    char raiz[4];
    const auto erro = monta_volume(raiz, sizeof raiz, log);
    if (erro != ErroCartao::Nenhum) {
        return erro;
    }

    char caminho[64];
    std::snprintf(caminho, sizeof caminho, "%s/%s", raiz, nome);
    FIL arquivo;
    const FRESULT abertura = f_open(&arquivo, caminho,
                                    FA_WRITE | FA_OPEN_APPEND);
    if (abertura != FR_OK) {
        f_unmount(raiz);
        return ErroCartao::FalhaDeEscrita;
    }
    UINT gravados = 0;
    const FRESULT r = f_write(&arquivo, conteudo,
                              static_cast<UINT>(tamanho), &gravados);
    f_close(&arquivo);
    f_unmount(raiz);
    return (r == FR_OK && gravados == tamanho) ? ErroCartao::Nenhum
                                               : ErroCartao::FalhaDeEscrita;
}

ErroCartao CartaoSd::grava_arquivo(const char* nome, const char* conteudo,
                                   std::size_t tamanho, Logger& log) {
    const auto erro = abre_para_escrita(nome, log);
    if (erro != ErroCartao::Nenhum) {
        return erro;
    }
    const bool ok = escreve(reinterpret_cast<const std::uint8_t*>(conteudo),
                            tamanho);
    const auto fechamento = conclui_escrita(log);
    if (!ok) {
        return ErroCartao::FalhaDeEscrita;
    }
    return fechamento;
}

bool CartaoSd::existe(const char* nome, Logger& log) {
    char raiz[4];
    if (monta_volume(raiz, sizeof raiz, log) != ErroCartao::Nenhum) {
        return false;
    }
    char caminho[64];
    std::snprintf(caminho, sizeof caminho, "%s/%s", raiz, nome);
    FILINFO info;
    const bool achou = f_stat(caminho, &info) == FR_OK;
    f_unmount(raiz);
    return achou;
}

}  // namespace coruja
