/* Cabeçalho MÍNIMO de FatFs, só para o alvo de host.
 *
 * Não é um FatFs: é a fachada que o `CartaoSd.cpp` usa, e nada além. A
 * implementação está em `FatFsFalso.cpp`, sobre um mapa em memória.
 *
 * **Por que um dublê e não o FatFs de verdade sobre um disco em RAM.** O que
 * se quer verificar é a NOSSA lógica — a sondagem de partições do R-38, a
 * sequência da troca atômica, o mapeamento de erro do ADR 0010 —, não o
 * FatFs, que é maduro e não é nosso. E os casos que interessam são
 * justamente os difíceis de provocar num disco real: "o volume 0 não monta e
 * o 2 sim", "o rename falhou", "o cartão sumiu no meio da escrita". Num
 * dublê são um campo; num disco em RAM, uma obra.
 *
 * O `ffconf.h` incluído é o NOSSO, o mesmo do alvo Pico, então os
 * `static_assert` do `CartaoSd.cpp` verificam a configuração real.
 */
#pragma once
#include <cstddef>
#include <cstdint>

#include "ffconf.h"

using BYTE = std::uint8_t;
using UINT = unsigned int;
using WORD = std::uint16_t;
using DWORD = std::uint32_t;
using FSIZE_t = std::uint32_t;
using TCHAR = char;

enum FRESULT {
    FR_OK = 0,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_NO_PATH,
    FR_INVALID_NAME,
    FR_DENIED,
    FR_EXIST,
    FR_INVALID_OBJECT,
    FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE,
    FR_NOT_ENABLED,
    FR_NO_FILESYSTEM,
    FR_TIMEOUT,
};

#define FA_READ          0x01
#define FA_WRITE         0x02
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_APPEND   0x30

struct FATFS { BYTE reservado; };

/// Mapa de volume lógico para partição física. O `CartaoSd.cpp` define o
/// `VolToPart[]` de verdade, e ele precisa do tipo mesmo no host — é a
/// tabela do R-38: volume 0 automático, 1 a 4 forçados nas primárias.
struct PARTITION { BYTE pd; BYTE pt; };

struct FIL {
    struct { FSIZE_t objsize; } obj;
    int  descritor;   ///< índice interno do dublê; -1 = fechado
};

struct FILINFO { FSIZE_t fsize; char fname[64]; };

#define f_size(fp) ((fp)->obj.objsize)

// Ligação C, como nos cabeçalhos do FatFs de verdade — e o `CartaoSd.cpp`
// conta com isso ao envolver os includes em `extern "C"`.
extern "C" {

FRESULT f_mount(FATFS* fs, const TCHAR* caminho, BYTE opt);
FRESULT f_unmount(const TCHAR* caminho);
FRESULT f_open(FIL* fp, const TCHAR* caminho, BYTE modo);
FRESULT f_close(FIL* fp);
FRESULT f_read(FIL* fp, void* destino, UINT quantos, UINT* lidos);
FRESULT f_write(FIL* fp, const void* origem, UINT quantos, UINT* escritos);
FRESULT f_stat(const TCHAR* caminho, FILINFO* info);
FRESULT f_rename(const TCHAR* de, const TCHAR* para);
FRESULT f_unlink(const TCHAR* caminho);

}  // extern "C"
