#include "nucleo/Nmea.h"

namespace coruja {
namespace {

/// Um campo da sentença, como recorte do buffer original. Sem cópia e sem
/// heap: a sentença chega uma vez e é lida no lugar.
struct Campo {
    const char* ini = nullptr;
    std::size_t n = 0;
    bool vazio() const { return n == 0; }
};

int valor_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/// Comprimento útil: tira `\r`, `\n` e espaços do fim. A UART entrega a
/// sentença com terminador de linha, e ele não faz parte do checksum.
std::size_t sem_terminador(const char* s, std::size_t n) {
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' ||
                     s[n - 1] == ' '  || s[n - 1] == '\t')) {
        --n;
    }
    return n;
}

/// Posição do `*`, ou `n` se não houver.
std::size_t posicao_asterisco(const char* s, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (s[i] == '*') return i;
    }
    return n;
}

/// Inteiro sem sinal, exigindo **todos** os caracteres dígitos. Devolve false
/// para campo vazio ou com qualquer caractere estranho — nada de `atoi`, que
/// devolve 0 para lixo e não distingue "zero" de "não é número".
bool inteiro(const Campo& c, std::uint32_t* destino) {
    if (c.vazio()) return false;
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < c.n; ++i) {
        if (c.ini[i] < '0' || c.ini[i] > '9') return false;
        v = v * 10U + static_cast<std::uint32_t>(c.ini[i] - '0');
    }
    *destino = v;
    return true;
}

/// Decimal com ponto, em `double`. Sem `strtod`: o campo não é terminado em
/// `\0` e copiá-lo para um buffer só para isso seria trabalho por nada.
bool decimal(const Campo& c, double* destino) {
    if (c.vazio()) return false;
    double inteiro_parte = 0.0;
    double fracao = 0.0;
    double divisor = 1.0;
    bool   depois_do_ponto = false;
    bool   algum_digito = false;
    std::size_t i = 0;
    bool negativo = false;

    if (c.ini[0] == '-' || c.ini[0] == '+') {
        negativo = c.ini[0] == '-';
        i = 1;
    }
    for (; i < c.n; ++i) {
        const char ch = c.ini[i];
        if (ch == '.') {
            if (depois_do_ponto) return false;  // dois pontos
            depois_do_ponto = true;
            continue;
        }
        if (ch < '0' || ch > '9') return false;
        algum_digito = true;
        if (depois_do_ponto) {
            divisor *= 10.0;
            fracao += static_cast<double>(ch - '0') / divisor;
        } else {
            inteiro_parte = inteiro_parte * 10.0 + (ch - '0');
        }
    }
    if (!algum_digito) return false;
    *destino = (inteiro_parte + fracao) * (negativo ? -1.0 : 1.0);
    return true;
}

/// `ddmm.mmmm` (latitude) ou `dddmm.mmmm` (longitude) para graus decimais.
///
/// O NMEA junta grau e minuto no mesmo número, e é onde parsers erram: os
/// minutos são sempre os **dois últimos** dígitos antes do ponto, e o que vem
/// antes é o grau — não há separador.
bool graus_minutos(const Campo& c, std::size_t digitos_de_grau, double* destino) {
    if (c.n < digitos_de_grau + 2) return false;

    Campo parte_grau{c.ini, digitos_de_grau};
    std::uint32_t g = 0;
    if (!inteiro(parte_grau, &g)) return false;

    Campo parte_minuto{c.ini + digitos_de_grau, c.n - digitos_de_grau};
    double m = 0.0;
    if (!decimal(parte_minuto, &m)) return false;
    if (m < 0.0 || m >= 60.0) return false;

    *destino = static_cast<double>(g) + m / 60.0;
    return true;
}

constexpr double kNoParaKmh = 1.852;
constexpr std::size_t kCamposRmc = 10;  ///< até a data, que é o índice 9

}  // namespace

const char* descreve(ErroNmea erro) {
    switch (erro) {
        case ErroNmea::Nenhum:             return "ok";
        case ErroNmea::Vazia:              return "sentenca vazia";
        case ErroNmea::SemCifrao:          return "nao comeca com '$'";
        case ErroNmea::SemAsterisco:       return "sem '*' de checksum";
        case ErroNmea::ChecksumMalformado: return "checksum sem dois digitos hex";
        case ErroNmea::ChecksumInvalido:   return "checksum nao confere";
        case ErroNmea::NaoEhRmc:           return "nao e uma sentenca RMC";
        case ErroNmea::CamposDeMenos:      return "campos de menos";
        case ErroNmea::SemFix:             return "sem fix (status V)";
        case ErroNmea::CampoInvalido:      return "campo com conteudo invalido";
    }
    return "erro desconhecido";
}

bool checksum_valido(const char* sentenca, std::size_t tamanho) {
    if (sentenca == nullptr) return false;
    const std::size_t n = sem_terminador(sentenca, tamanho);
    if (n < 4 || sentenca[0] != '$') return false;

    const std::size_t ast = posicao_asterisco(sentenca, n);
    if (ast >= n || n - ast != 3) return false;  // precisa de exatamente 2 dígitos

    const int alto = valor_hex(sentenca[ast + 1]);
    const int baixo = valor_hex(sentenca[ast + 2]);
    if (alto < 0 || baixo < 0) return false;

    std::uint8_t calculado = 0;
    for (std::size_t i = 1; i < ast; ++i) {
        calculado ^= static_cast<std::uint8_t>(sentenca[i]);
    }
    return calculado == static_cast<std::uint8_t>(alto * 16 + baixo);
}

ErroNmea analisa_rmc(const char* sentenca, std::size_t tamanho,
                     Telemetria* destino) {
    if (sentenca == nullptr || destino == nullptr || tamanho == 0) {
        return ErroNmea::Vazia;
    }
    const std::size_t n = sem_terminador(sentenca, tamanho);
    if (n == 0) return ErroNmea::Vazia;
    if (sentenca[0] != '$') return ErroNmea::SemCifrao;

    const std::size_t ast = posicao_asterisco(sentenca, n);
    if (ast >= n) return ErroNmea::SemAsterisco;
    if (n - ast != 3 || valor_hex(sentenca[ast + 1]) < 0 ||
        valor_hex(sentenca[ast + 2]) < 0) {
        return ErroNmea::ChecksumMalformado;
    }
    if (!checksum_valido(sentenca, tamanho)) return ErroNmea::ChecksumInvalido;

    // Layout do cabeçalho: `$` + 2 do talker + `RMC` + `,`.
    //   índice  0  1 2  3 4 5  6
    //           $  G N  R M C  ,
    // O talker não é conferido de propósito — ver a nota no cabeçalho.
    if (ast < 7 || sentenca[3] != 'R' || sentenca[4] != 'M' ||
        sentenca[5] != 'C' || sentenca[6] != ',') {
        return ErroNmea::NaoEhRmc;
    }

    // Recorta os campos entre vírgulas, do byte 7 até o `*`. Campo vazio é
    // recorte de comprimento zero, e não some — a posição importa.
    Campo campos[kCamposRmc];
    std::size_t quantos = 0;
    std::size_t inicio = 7;
    for (std::size_t i = 7; i <= ast && quantos < kCamposRmc; ++i) {
        if (i == ast || sentenca[i] == ',') {
            campos[quantos].ini = sentenca + inicio;
            campos[quantos].n = i - inicio;
            ++quantos;
            inicio = i + 1;
        }
    }
    if (quantos < kCamposRmc) return ErroNmea::CamposDeMenos;

    // 0=hora 1=status 2=lat 3=N/S 4=lon 5=E/W 6=nos 7=rumo 8=data
    const Campo& c_hora   = campos[0];
    const Campo& c_status = campos[1];
    const Campo& c_lat    = campos[2];
    const Campo& c_ns     = campos[3];
    const Campo& c_lon    = campos[4];
    const Campo& c_ew     = campos[5];
    const Campo& c_nos    = campos[6];
    const Campo& c_rumo   = campos[7];
    const Campo& c_data   = campos[8];

    if (c_status.n != 1) return ErroNmea::CampoInvalido;
    // Sem fix: sentença íntegra, dado ausente. Não é erro de formato, e não
    // pode sobrescrever a última posição conhecida.
    if (c_status.ini[0] != 'A') return ErroNmea::SemFix;

    Telemetria t;

    double lat = 0.0, lon = 0.0;
    if (!graus_minutos(c_lat, 2, &lat)) return ErroNmea::CampoInvalido;
    if (!graus_minutos(c_lon, 3, &lon)) return ErroNmea::CampoInvalido;
    if (c_ns.n != 1 || (c_ns.ini[0] != 'N' && c_ns.ini[0] != 'S')) {
        return ErroNmea::CampoInvalido;
    }
    if (c_ew.n != 1 || (c_ew.ini[0] != 'E' && c_ew.ini[0] != 'W')) {
        return ErroNmea::CampoInvalido;
    }
    if (lat > 90.0 || lon > 180.0) return ErroNmea::CampoInvalido;
    if (c_ns.ini[0] == 'S') lat = -lat;
    if (c_ew.ini[0] == 'W') lon = -lon;
    t.lat = static_cast<float>(lat);
    t.lon = static_cast<float>(lon);

    double nos = 0.0;
    if (!decimal(c_nos, &nos) || nos < 0.0) return ErroNmea::CampoInvalido;
    t.velocidade_kmh = static_cast<float>(nos * kNoParaKmh);

    // Rumo vazio com o veículo parado é normal, não erro.
    double rumo = 0.0;
    if (!c_rumo.vazio()) {
        if (!decimal(c_rumo, &rumo) || rumo < 0.0 || rumo >= 360.0) {
            return ErroNmea::CampoInvalido;
        }
        t.rumo_graus = static_cast<float>(rumo);
        t.rumo_valido = true;
    }

    // Hora `hhmmss.sss` e data `ddmmyy`. Ausentes não invalidam a posição — o
    // relógio é conveniência; a posição é o que protege o motorista.
    if (c_hora.n >= 6 && c_data.n == 6) {
        std::uint32_t hh = 0, mm = 0, ss = 0, dd = 0, mo = 0, aa = 0;
        const bool ok =
            inteiro(Campo{c_hora.ini, 2}, &hh) &&
            inteiro(Campo{c_hora.ini + 2, 2}, &mm) &&
            inteiro(Campo{c_hora.ini + 4, 2}, &ss) &&
            inteiro(Campo{c_data.ini, 2}, &dd) &&
            inteiro(Campo{c_data.ini + 2, 2}, &mo) &&
            inteiro(Campo{c_data.ini + 4, 2}, &aa);
        if (ok && hh < 24 && mm < 60 && ss < 60 &&
            dd >= 1 && dd <= 31 && mo >= 1 && mo <= 12) {
            t.hora = static_cast<std::uint8_t>(hh);
            t.minuto = static_cast<std::uint8_t>(mm);
            t.segundo = static_cast<std::uint8_t>(ss);
            t.dia = static_cast<std::uint8_t>(dd);
            t.mes = static_cast<std::uint8_t>(mo);
            t.ano = static_cast<std::uint16_t>(2000 + aa);
            t.data_valida = true;
        }
    }

    *destino = t;
    return ErroNmea::Nenhum;
}

}  // namespace coruja
