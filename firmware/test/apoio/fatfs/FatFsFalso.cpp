#include "apoio/fatfs/FatFsFalso.h"

#include <cstdio>
#include <cstring>
#include <iterator>

#include "armazenamento/CartaoSd.h"
#include "f_util.h"
#include "hw_config.h"

namespace coruja::teste {
namespace {
/// Definidas adiante, junto do estado interno que elas limpam.
void limpa_abertos();
void limpa_donos();
}  // namespace

FatFsFalso& FatFsFalso::instancia() {
    static FatFsFalso unico;
    return unico;
}

void FatFsFalso::reinicia() {
    for (auto& v : volumes) { v = VolumeFalso{}; }
    driver_inicia = true;
    erro_rename = FR_OK;
    erro_unlink = FR_OK;
    erro_opendir = FR_OK;
    erro_readdir = FR_OK;
    erro_close = FR_OK;
    diretorios.clear();
    escrita_falha_apos = 0;
    escrita_curta_apos = 0;
    leitura_falha_apos = 0;
    queda_apos.clear();
    queda_apos_n = 0;
    sem_energia = false;
    agora_ms = 0;
    operacoes.clear();
    montagens = 0;
    desmontagens = 0;
    // Descritores de uma rodada anterior nao podem vazar para a proxima: um
    // teste que esquece de fechar faria o seguinte falhar por motivo alheio.
    limpa_abertos();
    limpa_donos();
}

void FatFsFalso::religa() {
    queda_apos.clear();
    queda_apos_n = 0;
    sem_energia = false;
    for (auto& v : volumes) { v.montado = false; }
    limpa_abertos();
    limpa_donos();
}

bool FatFsFalso::existe(int volume, const std::string& nome) const {
    const auto& a = volumes[volume].arquivos;
    return a.find(nome) != a.end();
}

std::string FatFsFalso::conteudo(int volume, const std::string& nome) const {
    const auto& a = volumes[volume].arquivos;
    const auto it = a.find(nome);
    return it == a.end() ? std::string{} : it->second;
}

bool FatFsFalso::fez(const std::string& operacao) const {
    for (const auto& o : operacoes) { if (o == operacao) { return true; } }
    return false;
}

namespace {

/// Um arquivo aberto, indexado pelo `FIL*` de quem abriu.
///
/// Deixou de ser um só em 2026-10-06: a remessa de dados mantém um arquivo
/// aberto para leitura durante todo o envio, enquanto o log em cartão pode
/// abrir e fechar o `coruja.log` por mensagem no meio disso. O `CartaoSd`
/// passou a ter dois descritores, e um dublê com um só esconderia justamente
/// o conflito que motivou a separação.
struct Aberto {
    bool        usado = false;
    int         volume = 0;
    /// A montagem em que foi aberto. Ver `VolumeFalso::geracao`.
    unsigned    geracao = 0;
    std::string nome;
    std::string dados;
    std::size_t posicao = 0;
    bool        escrita = false;
    std::size_t escritos = 0;
};
std::map<FIL*, Aberto> g_abertos;

/// Para que volume cada `FATFS` foi registrado por ultimo.
///
/// O `CartaoSd` tem UMA area de trabalho e a reaproveita entre volumes. No
/// FatFs de verdade, `f_mount` de uma area ja registrada noutro volume a
/// reinicia por baixo dele: o volume antigo perde a area e tudo que estava
/// aberto nele morre. O duble tem de saber disso para pegar quem monta por
/// cima de uma operacao em curso.
std::map<const FATFS*, int> g_dono;

void limpa_abertos() { g_abertos.clear(); }
void limpa_donos() { g_dono.clear(); }

/// O aberto de `fp`, ou nulo. Nunca cria: pedir leitura num descritor que
/// ninguém abriu é erro de quem chama, e inventar um esconderia isso.
Aberto* aberto_de(FIL* fp) {
    auto it = g_abertos.find(fp);
    return it == g_abertos.end() ? nullptr : &it->second;
}

/// Separa "2:/radares.bin" em volume 2 e nome "radares.bin". Aceita também
/// "2:" (sem arquivo), que é o que o `f_mount` recebe.
bool separa(const char* caminho, int* volume, std::string* nome) {
    if (caminho == nullptr || caminho[0] < '0' || caminho[0] > '9' ||
        caminho[1] != ':') {
        return false;
    }
    *volume = caminho[0] - '0';
    if (*volume < 0 || *volume >= FF_VOLUMES) { return false; }
    const char* resto = caminho + 2;
    while (*resto == '/') { ++resto; }
    *nome = resto;
    return true;
}

void anota(const std::string& o) {
    auto& f = FatFsFalso::instancia();
    f.operacoes.push_back(o);
    const bool pelo_nome = !f.queda_apos.empty() && o == f.queda_apos;
    const bool pela_conta =
        f.queda_apos_n != 0 && f.operacoes.size() == f.queda_apos_n;
    if (pelo_nome || pela_conta) {
        // A operacao anotada termina; a energia cai antes da SEGUINTE.
        f.queda_apos.clear();
        f.queda_apos_n = 0;
        f.sem_energia = true;
    }
}

/// Sem energia, nada responde -- e o que estava aberto e nao foi fechado
/// se perdeu, porque o tamanho de um arquivo FAT so vai ao diretorio no
/// fechamento.
bool sem_energia() {
    auto& f = FatFsFalso::instancia();
    if (!f.sem_energia) { return false; }
    limpa_abertos();
    for (auto& v : f.volumes) { v.montado = false; }
    return true;
}

/// O FIL aberto ainda vive num volume montado, na MESMA montagem?
///
/// O `f_mount(NULL, ...)` do FatFs zera o `fs_type` da area de trabalho, e o
/// `validate()` de toda operacao sobre um FIL ja aberto passa a devolver
/// `FR_INVALID_OBJECT`. E por este caminho que o ENVIO morria: o fluxo de
/// leitura ficava aberto e uma descarga de log desmontava o volume debaixo
/// dele. Remontar por cima da-lhe o mesmo fim.
bool objeto_vivo(FIL* fp) {
    const Aberto* a = aberto_de(fp);
    if (a == nullptr) { return false; }
    const auto& v = FatFsFalso::instancia().volumes[a->volume];
    return v.montado && v.geracao == a->geracao;
}

}  // namespace
}  // namespace coruja::teste

using coruja::teste::FatFsFalso;

namespace coruja::teste {
namespace {

/// O volume do caminho esta montado? Senao, `FR_NOT_ENABLED` -- a mesma
/// resposta que o FatFs de verdade da, e a que apareceu no cartao do aparelho.
bool montado_de(const TCHAR* caminho) {
    int v = 0;
    std::string nome;
    if (!separa(caminho, &v, &nome)) { return false; }
    return FatFsFalso::instancia().volumes[v].montado;
}

}  // namespace
}  // namespace coruja::teste


// O `CartaoSd.cpp` envolve os includes de FatFs em `extern "C"`, como se faz
// com biblioteca C. As definicoes do duble precisam da MESMA ligacao, senao
// o compilador concorda e o ligador nao.
extern "C" {

FRESULT f_mount(FATFS* fs, const TCHAR* caminho, BYTE) {
    auto& f = FatFsFalso::instancia();
    int v = 0;
    std::string nome;
    if (!coruja::teste::separa(caminho, &v, &nome)) { return FR_INVALID_DRIVE; }
    coruja::teste::anota("mount:" + std::to_string(v));
    if (coruja::teste::sem_energia()) { return FR_NOT_READY; }
    // A area de trabalho e reiniciada ANTES de tentar montar, como no FatFs:
    // o volume que a usava fica sem ela, monte este ou nao.
    const auto dono = coruja::teste::g_dono.find(fs);
    if (dono != coruja::teste::g_dono.end() && dono->second != v) {
        f.volumes[dono->second].montado = false;
    }
    coruja::teste::g_dono[fs] = v;
    if (!f.volumes[v].monta) { return f.volumes[v].erro_montagem; }
    f.volumes[v].montado = true;
    ++f.volumes[v].geracao;
    ++f.montagens;
    return FR_OK;
}

FRESULT f_unmount(const TCHAR* caminho) {
    auto& f = FatFsFalso::instancia();
    int v = 0;
    std::string nome;
    if (!coruja::teste::separa(caminho, &v, &nome)) { return FR_INVALID_DRIVE; }
    coruja::teste::anota("unmount:" + std::to_string(v));
    f.volumes[v].montado = false;
    for (auto it = coruja::teste::g_dono.begin();
         it != coruja::teste::g_dono.end();) {
        it = it->second == v ? coruja::teste::g_dono.erase(it) : std::next(it);
    }
    ++f.desmontagens;
    return FR_OK;
}

FRESULT f_open(FIL* fp, const TCHAR* caminho, BYTE modo) {
    if (coruja::teste::sem_energia()) { return FR_NOT_READY; }
    if (!coruja::teste::montado_de(caminho)) { return FR_NOT_ENABLED; }
    auto& f = FatFsFalso::instancia();
    int v = 0;
    std::string nome;
    if (!coruja::teste::separa(caminho, &v, &nome)) { return FR_INVALID_NAME; }
    if (!f.volumes[v].monta) { return FR_NOT_READY; }
    const bool tem = f.existe(v, nome);
    if ((modo & FA_READ) != 0 && !tem) { return FR_NO_FILE; }

    auto& a = coruja::teste::g_abertos[fp];
    a = coruja::teste::Aberto{};
    a.usado = true;
    a.volume = v;
    a.geracao = f.volumes[v].geracao;
    a.nome = nome;
    a.escrita = (modo & FA_WRITE) != 0;
    if ((modo & FA_CREATE_ALWAYS) != 0) {
        // Truncar vai ao cartao NA HORA, e nao no fechamento: o FatFs zera o
        // tamanho no diretorio e solta a cadeia de clusters dentro do
        // proprio `f_open`. Uma queda logo depois deixa o arquivo vazio -- e
        // e exatamente por isso que reescrever no lugar nao e atomico.
        a.dados.clear();
        f.volumes[v].arquivos[nome].clear();
    } else if (tem) {
        a.dados = f.conteudo(v, nome);
    }
    // FA_OPEN_APPEND posiciona no fim; leitura, no começo.
    a.posicao = ((modo & FA_OPEN_APPEND) == FA_OPEN_APPEND) ? a.dados.size() : 0;
    fp->obj.objsize = static_cast<FSIZE_t>(a.dados.size());
    fp->descritor = 0;
    coruja::teste::anota("open:" + std::to_string(v) + ":" + nome);
    return FR_OK;
}

FRESULT f_close(FIL* fp) {
    if (coruja::teste::sem_energia()) { return FR_NOT_READY; }
    auto& f = FatFsFalso::instancia();
    auto* a = coruja::teste::aberto_de(fp);
    if (a == nullptr || !a->usado) { return FR_INVALID_OBJECT; }
    const std::string nome = a->nome;
    FRESULT r = FR_OK;
    if (a->escrita) {
        if (f.erro_close != FR_OK) {
            r = f.erro_close;
        } else {
            f.volumes[a->volume].arquivos[a->nome] = a->dados;
        }
    }
    coruja::teste::g_abertos.erase(fp);
    fp->descritor = -1;
    coruja::teste::anota("close:" + nome);
    return r;
}

FRESULT f_read(FIL* fp, void* destino, UINT quantos, UINT* lidos) {
    if (coruja::teste::sem_energia()) { return FR_NOT_READY; }
    if (!coruja::teste::objeto_vivo(fp)) { return FR_INVALID_OBJECT; }
    auto& f = FatFsFalso::instancia();
    auto* ap = coruja::teste::aberto_de(fp);
    if (ap == nullptr || !ap->usado) { return FR_INVALID_OBJECT; }
    auto& a = *ap;
    if (f.leitura_falha_apos != 0 && a.posicao >= f.leitura_falha_apos) {
        *lidos = 0;
        return FR_DISK_ERR;
    }
    const std::size_t resta = a.dados.size() - a.posicao;
    const std::size_t n = quantos < resta ? quantos : resta;
    std::memcpy(destino, a.dados.data() + a.posicao, n);
    a.posicao += n;
    *lidos = static_cast<UINT>(n);
    return FR_OK;
}

FRESULT f_lseek(FIL* fp, FSIZE_t posicao) {
    if (coruja::teste::sem_energia()) { return FR_NOT_READY; }
    if (!coruja::teste::objeto_vivo(fp)) { return FR_INVALID_OBJECT; }
    auto* ap = coruja::teste::aberto_de(fp);
    if (ap == nullptr || !ap->usado) { return FR_INVALID_OBJECT; }
    if (posicao > ap->dados.size()) { return FR_INVALID_PARAMETER; }
    ap->posicao = posicao;
    return FR_OK;
}

FRESULT f_truncate(FIL* fp) {
    if (coruja::teste::sem_energia()) { return FR_NOT_READY; }
    if (!coruja::teste::objeto_vivo(fp)) { return FR_INVALID_OBJECT; }
    auto* ap = coruja::teste::aberto_de(fp);
    if (ap == nullptr || !ap->usado || !ap->escrita) { return FR_DENIED; }
    ap->dados.resize(ap->posicao);
    fp->obj.objsize = static_cast<FSIZE_t>(ap->dados.size());
    coruja::teste::anota("truncate:" + ap->nome);
    return FR_OK;
}

FRESULT f_opendir(DIR* dp, const TCHAR* caminho) {
    if (coruja::teste::sem_energia()) { return FR_NOT_READY; }
    if (!coruja::teste::montado_de(caminho)) { return FR_NOT_ENABLED; }
    auto& f = FatFsFalso::instancia();
    int v = 0;
    std::string nome;
    if (!coruja::teste::separa(caminho, &v, &nome)) { return FR_INVALID_NAME; }
    if (!f.volumes[v].monta) { return FR_NOT_READY; }
    if (f.erro_opendir != FR_OK) { return f.erro_opendir; }
    dp->volume = v;
    dp->indice = 0;
    coruja::teste::anota("opendir:" + std::to_string(v));
    return FR_OK;
}

FRESULT f_readdir(DIR* dp, FILINFO* info) {
    if (coruja::teste::sem_energia()) { return FR_NOT_READY; }
    auto& f = FatFsFalso::instancia();
    if (f.erro_readdir != FR_OK) { return f.erro_readdir; }
    const auto& arquivos = f.volumes[dp->volume].arquivos;
    if (dp->indice >= arquivos.size()) {
        // Nome vazio é o fim da enumeração, como no FatFs de verdade.
        info->fname[0] = '\0';
        info->fsize = 0;
        info->fattrib = 0;
        return FR_OK;
    }
    auto it = arquivos.begin();
    std::advance(it, static_cast<long>(dp->indice));
    ++dp->indice;
    std::snprintf(info->fname, sizeof info->fname, "%s", it->first.c_str());
    info->fsize = static_cast<FSIZE_t>(it->second.size());
    info->fattrib = f.diretorios.count(it->first) != 0 ? AM_DIR : 0;
    return FR_OK;
}

FRESULT f_closedir(DIR*) { return FR_OK; }

FRESULT f_write(FIL* fp, const void* origem, UINT quantos, UINT* escritos) {
    if (coruja::teste::sem_energia()) { return FR_NOT_READY; }
    if (!coruja::teste::objeto_vivo(fp)) { return FR_INVALID_OBJECT; }
    auto& f = FatFsFalso::instancia();
    auto* ap = coruja::teste::aberto_de(fp);
    if (ap == nullptr || !ap->usado) { return FR_INVALID_OBJECT; }
    auto& a = *ap;
    if (f.escrita_falha_apos != 0 &&
        a.escritos + quantos > f.escrita_falha_apos) {
        *escritos = 0;
        return FR_DISK_ERR;
    }
    std::size_t cabe = quantos;
    if (f.escrita_curta_apos != 0) {
        const std::size_t resta = a.escritos < f.escrita_curta_apos
                                      ? f.escrita_curta_apos - a.escritos : 0;
        cabe = resta < quantos ? resta : quantos;
    }
    // Escreve na POSICAO, como o FatFs: depois de um `f_lseek` para tras, o
    // que vem por cima substitui, e nao se acrescenta ao fim.
    a.dados.replace(a.posicao, cabe, static_cast<const char*>(origem), cabe);
    a.posicao += cabe;
    a.escritos += cabe;
    fp->obj.objsize = static_cast<FSIZE_t>(a.dados.size());
    *escritos = static_cast<UINT>(cabe);
    coruja::teste::anota("write:" + a.nome);
    return FR_OK;
}

FRESULT f_stat(const TCHAR* caminho, FILINFO* info) {
    if (coruja::teste::sem_energia()) { return FR_NOT_READY; }
    if (!coruja::teste::montado_de(caminho)) { return FR_NOT_ENABLED; }
    auto& f = FatFsFalso::instancia();
    int v = 0;
    std::string nome;
    if (!coruja::teste::separa(caminho, &v, &nome)) { return FR_INVALID_NAME; }
    if (!f.volumes[v].monta) { return FR_NOT_READY; }
    if (!f.existe(v, nome)) { return FR_NO_FILE; }
    if (info != nullptr) {
        info->fsize = static_cast<FSIZE_t>(f.conteudo(v, nome).size());
    }
    return FR_OK;
}

FRESULT f_rename(const TCHAR* de, const TCHAR* para) {
    if (coruja::teste::sem_energia()) { return FR_NOT_READY; }
    if (!coruja::teste::montado_de(de)) { return FR_NOT_ENABLED; }
    auto& f = FatFsFalso::instancia();
    int vd = 0, vp = 0;
    std::string nd, np;
    if (!coruja::teste::separa(de, &vd, &nd) ||
        !coruja::teste::separa(para, &vp, &np)) { return FR_INVALID_NAME; }
    if (f.erro_rename != FR_OK) {
        coruja::teste::anota("rename:" + nd + "->" + np);
        return f.erro_rename;
    }
    if (!f.existe(vd, nd)) { return FR_NO_FILE; }
    if (f.existe(vp, np)) { return FR_EXIST; }
    f.volumes[vp].arquivos[np] = f.conteudo(vd, nd);
    f.volumes[vd].arquivos.erase(nd);
    coruja::teste::anota("rename:" + nd + "->" + np);
    return FR_OK;
}

FRESULT f_unlink(const TCHAR* caminho) {
    if (coruja::teste::sem_energia()) { return FR_NOT_READY; }
    if (!coruja::teste::montado_de(caminho)) { return FR_NOT_ENABLED; }
    auto& f = FatFsFalso::instancia();
    int v = 0;
    std::string nome;
    if (!coruja::teste::separa(caminho, &v, &nome)) { return FR_INVALID_NAME; }
    if (f.erro_unlink != FR_OK) {
        coruja::teste::anota("unlink:" + nome);
        return f.erro_unlink;
    }
    if (!f.existe(v, nome)) { return FR_NO_FILE; }
    f.volumes[v].arquivos.erase(nome);
    coruja::teste::anota("unlink:" + nome);
    return FR_OK;
}

const char* FRESULT_str(FRESULT r) {
    switch (r) {
        case FR_OK:            return "FR_OK";
        case FR_DISK_ERR:      return "FR_DISK_ERR";
        case FR_NOT_READY:     return "FR_NOT_READY";
        case FR_NO_FILE:       return "FR_NO_FILE";
        case FR_NO_FILESYSTEM: return "FR_NO_FILESYSTEM";
        case FR_EXIST:         return "FR_EXIST";
        default:               return "FR_?";
    }
}

bool sd_init_driver() { return FatFsFalso::instancia().driver_inicia; }

}  // extern "C"

namespace coruja {
/// O relógio do `CartaoSd` no host: o teste o adianta pelo dublê. No alvo
/// Pico quem o define é o `hw_config.cpp`, com o relógio do SDK.
std::uint32_t agora_ms_cartao() {
    return teste::FatFsFalso::instancia().agora_ms;
}
}  // namespace coruja
