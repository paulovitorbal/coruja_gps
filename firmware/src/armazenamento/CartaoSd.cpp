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

/// Monta "<raiz>/<nome>" em `destino`. **Falso quando não cabe.**
///
/// ⚠️ O `snprintf` trunca em silêncio, e caminho truncado endereça OUTRO
/// arquivo: ler um e gravar por cima de outro é o tipo de defeito que não se
/// parece com a causa. O limite é do buffer, não do FAT -- o maior nome que o
/// firmware gera tem 19 caracteres.
bool monta_caminho(char* destino, std::size_t capacidade, const char* raiz,
                   const char* nome) {
    const int n = std::snprintf(destino, capacidade, "%s/%s", raiz,
                                nome != nullptr ? nome : "");
    return n > 0 && static_cast<std::size_t>(n) < capacidade;
}

/// Um objeto só, reaproveitado entre as tentativas. Cada `FATFS` carrega um
/// setor de 512 B, e cinco deles custariam 2,5 KiB para nada: as tentativas
/// são sequenciais e só uma fica montada por vez.
FATFS g_fs;
FIL   g_arquivo;
/// Descritor proprio da leitura em fluxo da remessa.
///
/// Separado do `g_arquivo` porque os dois caminhos coexistem: a remessa
/// mantem um arquivo aberto durante todo o envio, e o log em cartao -- se
/// ligado -- abre e fecha o `coruja.log` por mensagem no meio disso. Com um
/// descritor so, a primeira linha de log destruiria o handle da leitura.
FIL   g_leitura;

}  // namespace

bool CartaoSd::em_espera() const {
    return falhou_ && agora_ms_cartao() - falhou_em_ms_ < kEsperaRemontagemMs;
}

void CartaoSd::anota_cartao_ausente() {
    falhou_ = true;
    falhou_em_ms_ = agora_ms_cartao();
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
    log.debug("sd", "driver do cartao iniciado");
}

ErroCartao CartaoSd::le_do_volume(const char* raiz, const char* nome,
                                  char* destino, std::size_t capacidade,
                                  std::size_t* lidos, Logger& log) {
    char caminho[64], msg[128];
    if (!monta_caminho(caminho, sizeof caminho, raiz, nome)) {
        log.error("sd", "o caminho do arquivo nao cabe no buffer");
        return ErroCartao::FalhaDeLeitura;
    }

    if (f_open(&g_arquivo, caminho, FA_READ) != FR_OK) {
        std::snprintf(msg, sizeof msg, "'%s' montou, sem '%s'", raiz, nome);
        log.debug("sd", msg);
        return ErroCartao::ArquivoAusente;
    }

    const FSIZE_t tamanho = f_size(&g_arquivo);
    std::snprintf(msg, sizeof msg, "'%s' achado em '%s', %lu bytes", nome,
                  raiz, static_cast<unsigned long>(tamanho));
    // DEBUG: abrir um arquivo que existe nao e noticia. Eram 203 linhas
    // no historico de 108 partidas, uma por `open`.
    log.debug("sd", msg);

    if (tamanho >= capacidade) {
        f_close(&g_arquivo);
        return ErroCartao::ArquivoGrande;
    }

    UINT          obtidos = 0;
    const FRESULT leitura = f_read(&g_arquivo, destino,
                                   static_cast<UINT>(tamanho), &obtidos);
    f_close(&g_arquivo);

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

ErroCartao CartaoSd::le_arquivo(const char* nome, char* destino,
                                std::size_t capacidade, std::size_t* lidos,
                                Logger& log) {
    if (lidos != nullptr) {
        *lidos = 0;
    }
    if (escrevendo_) {
        // A escrita em fluxo -- o download da base -- usa o MESMO descritor.
        // Abrir outro arquivo nele corromperia os dois.
        log.error("sd", "leitura pedida durante uma escrita em fluxo");
        return ErroCartao::FalhaDeLeitura;
    }
    if (!iniciado_) {
        inicia(log);
    }

    if (montagens_ > 0) {
        // ⚠️ **Já há operação segurando um volume.** Sondar daqui chamaria
        // `f_mount` por cima dela: o FatFs reinicia a área de trabalho e todo
        // `FIL` aberto no volume morre -- e a remessa mantém um aberto
        // durante o envio inteiro. Usa o volume de quem está por fora.
        char raiz[4];
        if (monta_volume(raiz, sizeof raiz, log) != ErroCartao::Nenhum) {
            return ErroCartao::SemCartaoLegivel;
        }
        const auto r = le_do_volume(raiz, nome, destino, capacidade, lidos,
                                    log);
        desmonta_volume(raiz);
        return r;
    }

    if (em_espera()) {
        // A sondagem anterior nao achou cartao nenhum, e nao faz cinco
        // segundos. Sondar de novo so ocuparia o barramento.
        return ErroCartao::SemCartaoLegivel;
    }

    char msg[128];
    int  montadas = 0;

    // Sonda as partições **procurando o arquivo**, não a primeira que for FAT.
    // É a decisão do R-38: num cartão de duas partições, escolher pela
    // primeira FAT daria "sem configuracao" com o arquivo no cartão.
    for (int volume = 0; volume < kQuantosVolumes; ++volume) {
        char raiz[4];
        std::snprintf(raiz, sizeof raiz, "%d:", volume);

        // O `le_arquivo` sonda por conta propria (R-38: procura o ARQUIVO,
        // nao a primeira FAT), entao a contabilidade e feita aqui tambem --
        // senao o `desmonta_volume` logo abaixo nao teria o que soltar e o
        // volume ficaria montado para sempre.
        const FRESULT montagem = f_mount(&g_fs, raiz, 1);
        if (montagem == FR_OK) {
            std::snprintf(raiz_montada_, sizeof raiz_montada_, "%s", raiz);
            ++montagens_;
        }
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
            // DEBUG: num cartao de uma particao so, o volume 1 NUNCA monta --
            // e a linha saia em toda leitura de arquivo, dizendo que o
            // normal aconteceu.
            log.debug("sd", msg);
            continue;
        }
        ++montadas;

        const auto r = le_do_volume(raiz, nome, destino, capacidade, lidos,
                                    log);
        desmonta_volume(raiz);
        if (r == ErroCartao::ArquivoAusente) {
            continue;
        }
        if (r == ErroCartao::Nenhum) {
            // A base tem de acompanhar a configuração que a definiu: guarda o
            // volume para que a escrita vá ao mesmo lugar, sem sondar de novo.
            volume_ativo_ = volume;
            falhou_ = false;
        }
        return r;
    }

    if (montadas == 0) {
        // Sem pino de card detect, ausente e ilegível chegam aqui iguais — e
        // é assim que o projeto quer (ADR 0010).
        anota_cartao_ausente();
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
    // Quem sabe se o arquivo era obrigatorio e quem chamou, e e la que a
    // severidade pertence -- aqui a ausencia e so um fato de bancada.
    log.debug("sd", msg);
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
    if (!monta_caminho(caminho, sizeof caminho, raiz, nome)) {
        log.error("sd", "o caminho do arquivo nao cabe no buffer");
        desmonta_volume(raiz);
        return ErroCartao::FalhaDeLeitura;
    }
    const FRESULT abertura = f_open(&g_arquivo, caminho, FA_READ);
    if (abertura != FR_OK) {
        desmonta_volume(raiz);
        return ErroCartao::ArquivoAusente;
    }

    const FSIZE_t tamanho = f_size(&g_arquivo);
    std::snprintf(msg, sizeof msg, "lendo '%s' em fluxo: %lu bytes", nome,
                  static_cast<unsigned long>(tamanho));
    // O tamanho do `radares.bin` ja sai na linha da base, logo abaixo.
    log.debug("sd", msg);

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
    desmonta_volume(raiz);
    if (lidos != nullptr) {
        *lidos = total;
    }
    return saida;
}

// ---------------------------------------------------------------- escrita ---

ErroCartao CartaoSd::monta_volume(char* raiz, std::size_t tam_raiz, Logger& log) {
    if (montagens_ > 0) {
        // Ja ha operacao segurando o volume. Reaproveita a montagem dela em
        // vez de montar de novo -- e, principalmente, em vez de DESMONTAR no
        // fim e deixar quem esta por fora com um volume morto nas maos.
        std::snprintf(raiz, tam_raiz, "%s", raiz_montada_);
        ++montagens_;
        return ErroCartao::Nenhum;
    }
    if (!iniciado_) {
        inicia(log);
    }
    if (em_espera()) {
        return ErroCartao::SemCartaoLegivel;
    }
    // Volume conhecido: tenta ele primeiro. Não montando, **sonda os
    // outros** -- trocar o cartão por um em que tudo mora noutra partição
    // deixava a escrita presa na que sumiu, até religar o aparelho.
    const int preferido = volume_ativo_;

    char msg[96];
    for (int i = -1; i < kQuantosVolumes; ++i) {
        const int volume = i < 0 ? preferido : i;
        if (volume < 0 || (i >= 0 && volume == preferido)) {
            continue;  // sem preferido, ou já tentado acima
        }
        std::snprintf(raiz, tam_raiz, "%d:", volume);
        const FRESULT montagem = f_mount(&g_fs, raiz, 1);
        if (montagem == FR_OK) {
            volume_ativo_ = volume;
            std::snprintf(raiz_montada_, sizeof raiz_montada_, "%s", raiz);
            montagens_ = 1;
            falhou_ = false;
            return ErroCartao::Nenhum;
        }
        std::snprintf(msg, sizeof msg, "volume %d nao montou para escrita: %s",
                      volume, FRESULT_str(montagem));
        log.debug("sd", msg);
    }
    anota_cartao_ausente();
    log.error("sd", "nenhum volume montou para escrita");
    return ErroCartao::SemCartaoLegivel;
}

void CartaoSd::desmonta_volume(const char* raiz) {
    if (montagens_ == 0) {
        return;  // nada a soltar; desmontar aqui derrubaria o volume de outro
    }
    if (--montagens_ == 0) {
        f_unmount(raiz);
    }
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
    if (!monta_caminho(caminho, sizeof caminho, raiz_aberta_, nome)) {
        log.error("sd", "o caminho do arquivo nao cabe no buffer");
        desmonta_volume(raiz_aberta_);
        return ErroCartao::FalhaDeEscrita;
    }
    const FRESULT r = f_open(&g_arquivo, caminho, FA_WRITE | FA_CREATE_ALWAYS);
    if (r != FR_OK) {
        char msg[96];
        std::snprintf(msg, sizeof msg, "f_open('%s') para escrita: %s", caminho,
                      FRESULT_str(r));
        log.error("sd", msg);
        desmonta_volume(raiz_aberta_);
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
    desmonta_volume(raiz_aberta_);
    if (r != FR_OK) {
        char msg[64];
        std::snprintf(msg, sizeof msg, "f_close: %s", FRESULT_str(r));
        log.error("sd", msg);
        return ErroCartao::FalhaDeEscrita;
    }
    return ErroCartao::Nenhum;
}

void CartaoSd::descarta_escrita(const char* nome, Logger& log) {
    // ⚠️ **Não é "cancela a escrita aberta", é "este arquivo não deve
    // existir".** Era a primeira coisa, e por isso virava um no-op no caso
    // que mais importa: quando `conclui_escrita` falha ela já baixou a
    // bandeira e desmontou, e o `AtualizadorOta` chama o descarte logo
    // depois. O `radares.tmp` ficava no cartão, ocupando espaço e parecendo
    // um download bom.
    char raiz[4];
    if (escrevendo_) {
        f_close(&g_arquivo);
        escrevendo_ = false;
        std::snprintf(raiz, sizeof raiz, "%s", raiz_aberta_);
    } else if (monta_volume(raiz, sizeof raiz, log) != ErroCartao::Nenhum) {
        // Sem cartão não há o que apagar agora. O `.tmp` que ficou será
        // truncado pelo `FA_CREATE_ALWAYS` da próxima tentativa.
        return;
    }

    char caminho[64];
    if (!monta_caminho(caminho, sizeof caminho, raiz, nome)) {
        log.error("sd", "o caminho do arquivo nao cabe no buffer");
        desmonta_volume(raiz);
        return;
    }
    const FRESULT r = f_unlink(caminho);
    desmonta_volume(raiz);

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
    if (!monta_caminho(p_tmp,  sizeof p_tmp,  raiz, temporario) ||
        !monta_caminho(p_base, sizeof p_base, raiz, base) ||
        !monta_caminho(p_bak,  sizeof p_bak,  raiz, reserva)) {
        log.error("sd", "o caminho do arquivo nao cabe no buffer");
        desmonta_volume(raiz);
        return ErroCartao::FalhaDeRenomeacao;
    }

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
            desmonta_volume(raiz);
            return ErroCartao::FalhaDeRenomeacao;
        }
        std::snprintf(msg, sizeof msg, "base anterior guardada como '%s'", reserva);
        // DEBUG: o estado de viagem passa por aqui a cada minuto, e quem
        // promove ja diz no proprio log o que trocou.
        log.debug("sd", msg);
    }

    // Passo 4. Deste ponto em diante o cartão volta a estar consistente.
    const FRESULT r = f_rename(p_tmp, p_base);
    desmonta_volume(raiz);
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
    if (!monta_caminho(caminho, sizeof caminho, raiz, nome)) {
        log.error("sd", "o caminho do arquivo nao cabe no buffer");
        desmonta_volume(raiz);
        return ErroCartao::FalhaDeEscrita;
    }
    FIL arquivo;
    const FRESULT abertura = f_open(&arquivo, caminho,
                                    FA_WRITE | FA_OPEN_APPEND);
    if (abertura != FR_OK) {
        desmonta_volume(raiz);
        return ErroCartao::FalhaDeEscrita;
    }
    // Onde o arquivo terminava. É para cá que ele volta se a escrita não
    // couber inteira — ver abaixo.
    const FSIZE_t antes = f_size(&arquivo);

    UINT gravados = 0;
    const FRESULT r = f_write(&arquivo, conteudo,
                              static_cast<UINT>(tamanho), &gravados);
    // ⚠️ **Gravação curta é falha, e o pedaço TEM de sair.** Cartão cheio
    // devolve `FR_OK` com menos bytes. Quem chama é o log, que guarda o
    // buffer e tenta de novo: com o pedaço no arquivo, a tentativa seguinte
    // o repetia inteiro e o cartão ficava com meia linha colada numa linha
    // boa. O acréscimo é tudo ou nada.
    const bool coube = r == FR_OK && gravados == tamanho;
    if (!coube && f_lseek(&arquivo, antes) == FR_OK) {
        f_truncate(&arquivo);
    }
    // O `f_close` é quem leva o tamanho novo ao diretório: ignorá-lo era
    // dizer "gravado" sobre uma linha que não entrou.
    const FRESULT fechamento = f_close(&arquivo);
    desmonta_volume(raiz);
    if (coube && fechamento == FR_OK) {
        return ErroCartao::Nenhum;
    }
    char msg[96];
    std::snprintf(msg, sizeof msg, "acrescimo em '%s': %s (%u de %u bytes)",
                  nome, FRESULT_str(coube ? fechamento : r),
                  static_cast<unsigned>(gravados),
                  static_cast<unsigned>(tamanho));
    log.error("sd", msg);
    return ErroCartao::FalhaDeEscrita;
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

// ---------------------------------------------------------------------------
// Arquivario -- o que a remessa de dados usa
// ---------------------------------------------------------------------------

ErroCartao CartaoSd::lista(AoListar ao_listar, void* contexto, Logger& log) {
    if (ao_listar == nullptr) {
        return ErroCartao::Nenhum;
    }
    char raiz[4] = {};
    const auto erro = monta_volume(raiz, sizeof raiz, log);
    if (erro != ErroCartao::Nenhum) {
        return erro;
    }

    DIR    dir = {};
    FILINFO info = {};
    const FRESULT abertura = f_opendir(&dir, raiz);
    if (abertura != FR_OK) {
        char msg[96];
        std::snprintf(msg, sizeof msg, "f_opendir('%s'): %s", raiz,
                      FRESULT_str(abertura));
        log.error("sd", msg);
        desmonta_volume(raiz);
        return ErroCartao::FalhaDeLeitura;
    }

    ErroCartao saida = ErroCartao::Nenhum;
    for (;;) {
        const FRESULT r = f_readdir(&dir, &info);
        if (r != FR_OK) {
            char msg[96];
            std::snprintf(msg, sizeof msg, "f_readdir: %s", FRESULT_str(r));
            log.error("sd", msg);
            saida = ErroCartao::FalhaDeLeitura;
            break;
        }
        // Nome vazio e o fim da enumeracao, nao um erro.
        if (info.fname[0] == '\0') {
            break;
        }
        if ((info.fattrib & AM_DIR) != 0) {
            continue;
        }
        ao_listar(contexto, info.fname, static_cast<std::size_t>(info.fsize));
    }

    f_closedir(&dir);
    desmonta_volume(raiz);
    return saida;
}

ErroCartao CartaoSd::abre_para_leitura(const char* nome, std::size_t* tamanho,
                                       Logger& log) {
    if (lendo_) {
        log.error("sd", "abre_para_leitura com uma leitura ja em curso");
        return ErroCartao::FalhaDeLeitura;
    }
    if (nome == nullptr || tamanho == nullptr) {
        return ErroCartao::FalhaDeLeitura;
    }
    const auto erro = monta_volume(raiz_leitura_, sizeof raiz_leitura_, log);
    if (erro != ErroCartao::Nenhum) {
        return erro;
    }

    char caminho[64];
    if (!monta_caminho(caminho, sizeof caminho, raiz_leitura_, nome)) {
        log.error("sd", "o caminho do arquivo nao cabe no buffer");
        desmonta_volume(raiz_leitura_);
        return ErroCartao::FalhaDeLeitura;
    }
    const FRESULT r = f_open(&g_leitura, caminho, FA_READ);
    if (r != FR_OK) {
        desmonta_volume(raiz_leitura_);
        // Ausente nao e falha de leitura: a remessa listou o cartao e o
        // arquivo pode ter sido apagado entre a listagem e agora.
        return r == FR_NO_FILE || r == FR_NO_PATH ? ErroCartao::ArquivoAusente
                                                  : ErroCartao::FalhaDeLeitura;
    }
    *tamanho = static_cast<std::size_t>(f_size(&g_leitura));
    lendo_ = true;
    return ErroCartao::Nenhum;
}

bool CartaoSd::le(std::uint8_t* destino, std::size_t capacidade,
                  std::size_t* lidos) {
    if (!lendo_ || destino == nullptr || lidos == nullptr) {
        return false;
    }
    UINT obtidos = 0;
    const FRESULT r = f_read(&g_leitura, destino,
                             static_cast<UINT>(capacidade), &obtidos);
    if (r != FR_OK) {
        return false;
    }
    *lidos = obtidos;
    return true;
}

bool CartaoSd::rebobina() {
    return lendo_ && f_lseek(&g_leitura, 0) == FR_OK;
}

void CartaoSd::fecha_leitura() {
    if (!lendo_) {
        return;
    }
    f_close(&g_leitura);
    desmonta_volume(raiz_leitura_);
    lendo_ = false;
}

ErroCartao CartaoSd::remove(const char* nome, Logger& log) {
    if (nome == nullptr) {
        return ErroCartao::ArquivoAusente;
    }
    char raiz[4] = {};
    const auto erro = monta_volume(raiz, sizeof raiz, log);
    if (erro != ErroCartao::Nenhum) {
        return erro;
    }
    char caminho[64];
    if (!monta_caminho(caminho, sizeof caminho, raiz, nome)) {
        log.error("sd", "o caminho do arquivo nao cabe no buffer");
        desmonta_volume(raiz);
        return ErroCartao::FalhaDeEscrita;
    }
    const FRESULT r = f_unlink(caminho);
    desmonta_volume(raiz);
    if (r == FR_OK) {
        return ErroCartao::Nenhum;
    }
    if (r == FR_NO_FILE || r == FR_NO_PATH) {
        return ErroCartao::ArquivoAusente;
    }
    char msg[96];
    std::snprintf(msg, sizeof msg, "f_unlink('%s'): %s", caminho,
                  FRESULT_str(r));
    log.error("sd", msg);
    return ErroCartao::FalhaDeEscrita;
}

}  // namespace coruja
