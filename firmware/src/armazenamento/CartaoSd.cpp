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
    std::snprintf(msg, sizeof msg,
                  "'%s' nao esta em nenhuma das %d particoes FAT montadas",
                  nome, montadas);
    log.error("sd", msg);
    return ErroCartao::ArquivoAusente;
}

}  // namespace coruja
