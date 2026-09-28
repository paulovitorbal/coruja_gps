#include "apoio/fatfs/FatFsFalso.h"

#include <cstring>

#include "f_util.h"
#include "hw_config.h"

namespace coruja::teste {

FatFsFalso& FatFsFalso::instancia() {
    static FatFsFalso unico;
    return unico;
}

void FatFsFalso::reinicia() {
    for (auto& v : volumes) { v = VolumeFalso{}; }
    driver_inicia = true;
    erro_rename = FR_OK;
    erro_unlink = FR_OK;
    escrita_falha_apos = 0;
    operacoes.clear();
    montagens = 0;
    desmontagens = 0;
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

/// Um arquivo aberto. Só um por vez basta: o `CartaoSd` nunca abre dois.
struct Aberto {
    bool        usado = false;
    int         volume = 0;
    std::string nome;
    std::string dados;
    std::size_t posicao = 0;
    bool        escrita = false;
    std::size_t escritos = 0;
};
Aberto g_aberto;

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
    FatFsFalso::instancia().operacoes.push_back(o);
}

}  // namespace
}  // namespace coruja::teste

using coruja::teste::FatFsFalso;

// O `CartaoSd.cpp` envolve os includes de FatFs em `extern "C"`, como se faz
// com biblioteca C. As definicoes do duble precisam da MESMA ligacao, senao
// o compilador concorda e o ligador nao.
extern "C" {

FRESULT f_mount(FATFS*, const TCHAR* caminho, BYTE) {
    auto& f = FatFsFalso::instancia();
    int v = 0;
    std::string nome;
    if (!coruja::teste::separa(caminho, &v, &nome)) { return FR_INVALID_DRIVE; }
    coruja::teste::anota("mount:" + std::to_string(v));
    if (!f.volumes[v].monta) { return f.volumes[v].erro_montagem; }
    ++f.montagens;
    return FR_OK;
}

FRESULT f_unmount(const TCHAR* caminho) {
    auto& f = FatFsFalso::instancia();
    int v = 0;
    std::string nome;
    if (!coruja::teste::separa(caminho, &v, &nome)) { return FR_INVALID_DRIVE; }
    coruja::teste::anota("unmount:" + std::to_string(v));
    ++f.desmontagens;
    return FR_OK;
}

FRESULT f_open(FIL* fp, const TCHAR* caminho, BYTE modo) {
    auto& f = FatFsFalso::instancia();
    auto& a = coruja::teste::g_aberto;
    int v = 0;
    std::string nome;
    if (!coruja::teste::separa(caminho, &v, &nome)) { return FR_INVALID_NAME; }
    if (!f.volumes[v].monta) { return FR_NOT_READY; }
    const bool tem = f.existe(v, nome);
    if ((modo & FA_READ) != 0 && !tem) { return FR_NO_FILE; }

    a = coruja::teste::Aberto{};
    a.usado = true;
    a.volume = v;
    a.nome = nome;
    a.escrita = (modo & FA_WRITE) != 0;
    if ((modo & FA_CREATE_ALWAYS) != 0) {
        a.dados.clear();
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
    auto& f = FatFsFalso::instancia();
    auto& a = coruja::teste::g_aberto;
    if (!a.usado) { return FR_INVALID_OBJECT; }
    if (a.escrita) { f.volumes[a.volume].arquivos[a.nome] = a.dados; }
    a.usado = false;
    fp->descritor = -1;
    return FR_OK;
}

FRESULT f_read(FIL*, void* destino, UINT quantos, UINT* lidos) {
    auto& a = coruja::teste::g_aberto;
    if (!a.usado) { return FR_INVALID_OBJECT; }
    const std::size_t resta = a.dados.size() - a.posicao;
    const std::size_t n = quantos < resta ? quantos : resta;
    std::memcpy(destino, a.dados.data() + a.posicao, n);
    a.posicao += n;
    *lidos = static_cast<UINT>(n);
    return FR_OK;
}

FRESULT f_write(FIL*, const void* origem, UINT quantos, UINT* escritos) {
    auto& f = FatFsFalso::instancia();
    auto& a = coruja::teste::g_aberto;
    if (!a.usado) { return FR_INVALID_OBJECT; }
    if (f.escrita_falha_apos != 0 &&
        a.escritos + quantos > f.escrita_falha_apos) {
        *escritos = 0;
        return FR_DISK_ERR;
    }
    a.dados.append(static_cast<const char*>(origem), quantos);
    a.escritos += quantos;
    *escritos = quantos;
    return FR_OK;
}

FRESULT f_stat(const TCHAR* caminho, FILINFO* info) {
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
    auto& f = FatFsFalso::instancia();
    int vd = 0, vp = 0;
    std::string nd, np;
    if (!coruja::teste::separa(de, &vd, &nd) ||
        !coruja::teste::separa(para, &vp, &np)) { return FR_INVALID_NAME; }
    coruja::teste::anota("rename:" + nd + "->" + np);
    if (f.erro_rename != FR_OK) { return f.erro_rename; }
    if (!f.existe(vd, nd)) { return FR_NO_FILE; }
    if (f.existe(vp, np)) { return FR_EXIST; }
    f.volumes[vp].arquivos[np] = f.conteudo(vd, nd);
    f.volumes[vd].arquivos.erase(nd);
    return FR_OK;
}

FRESULT f_unlink(const TCHAR* caminho) {
    auto& f = FatFsFalso::instancia();
    int v = 0;
    std::string nome;
    if (!coruja::teste::separa(caminho, &v, &nome)) { return FR_INVALID_NAME; }
    coruja::teste::anota("unlink:" + nome);
    if (f.erro_unlink != FR_OK) { return f.erro_unlink; }
    if (!f.existe(v, nome)) { return FR_NO_FILE; }
    f.volumes[v].arquivos.erase(nome);
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
