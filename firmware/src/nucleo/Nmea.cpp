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
        const auto d = static_cast<std::uint32_t>(c.ini[i] - '0');
        // Sem esta guarda o `uint32_t` dá a volta em silêncio, e 2^32 + 9
        // satélites viram 9 -- um número plausível.
        if (v > (UINT32_MAX - d) / 10U) return false;
        v = v * 10U + d;
    }
    *destino = v;
    return true;
}

/// Decimal **sem sinal**, com ponto, em `double`. Sem `strtod`: o campo não é
/// terminado em `\0` e copiá-lo para um buffer só para isso seria trabalho
/// por nada.
///
/// Sem sinal porque nenhum campo que lê o tem: a RMC diz o hemisfério por
/// letra, e velocidade, rumo, minutos e HDOP não são negativos. Aceitá-lo
/// deixava `+` e `-` de lixo passarem, e obrigava cada chamador a repetir o
/// "e não pode ser negativo".
// ⚠️ **`double` aqui é deliberado, contra a regra do resto do projeto.**
//
// O `Geo.h` documenta que a FPU do RP2350 é de precisão simples e que é por
// isso que a distância é equirretangular em `float`. Aqui é o contrário, e a
// razão é a largura do dado: uma latitude como `-15,857512` pede ~8 dígitos
// significativos, e `float` entrega ~7. Fazer o parsing em `float` custaria
// o último dígito, que vale cerca de **1 m** de posição.
//
// O custo é software float, porque a FPU não faz `double` — mas isto roda 4
// vezes por segundo sobre uma sentença, não num laço. O resultado é
// estreitado para `float` ao entrar na `Telemetria`; o `double` existe só
// para a conta intermediária não acumular erro antes disso.
//
// Quem "padronizar" estas funções para `float` seguindo a regra declarada
// perderá precisão de posição em silêncio. É por isso que esta nota existe.
bool decimal(const Campo& c, double* destino) {
    if (c.vazio()) return false;
    double inteiro_parte = 0.0;
    double fracao = 0.0;
    double divisor = 1.0;
    bool   depois_do_ponto = false;
    bool   algum_digito = false;
    for (std::size_t i = 0; i < c.n; ++i) {
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
    *destino = inteiro_parte + fracao;
    return true;
}

/// `ddmm.mmmm` (latitude) ou `dddmm.mmmm` (longitude) para graus decimais.
///
/// O NMEA junta grau e minuto no mesmo número, e é onde parsers erram: os
/// minutos são sempre os **dois últimos** dígitos antes do ponto, e o que vem
/// antes é o grau — não há separador. Por isso o ponto é exigido exatamente
/// ali: em qualquer outra posição a coluna está deslocada, e a conta daria
/// uma posição plausível e errada.
bool graus_minutos(const Campo& c, std::size_t digitos_de_grau, double* destino) {
    const std::size_t ponto = digitos_de_grau + 2;
    if (c.n <= ponto || c.ini[ponto] != '.') return false;

    Campo parte_grau{c.ini, digitos_de_grau};
    std::uint32_t g = 0;
    if (!inteiro(parte_grau, &g)) return false;

    Campo parte_minuto{c.ini + digitos_de_grau, c.n - digitos_de_grau};
    double m = 0.0;
    if (!decimal(parte_minuto, &m) || m >= 60.0) return false;

    *destino = static_cast<double>(g) + m / 60.0;
    return true;
}

constexpr double kNoParaKmh = 1.852;

/// Teto da velocidade. O NEO-M8N não reporta acima de 500 m/s (~972 nós);
/// acima disto é campo enorme e bem formado que passou pelo checksum, e
/// estreitá-lo para `float` seria comportamento indefinido.
constexpr double kMaxNos = 1000.0;

/// Teto do HDOP. O u-blox o satura em 99,99; o resto é lixo, e o mesmo
/// estreitamento para `float` cobra o mesmo teto.
constexpr double kMaxHdop = 100.0;

/// Até a data, que é o índice 8. Variação magnética e modo vêm depois e são
/// opcionais: exigi-los recusava uma RMC completa.
constexpr std::size_t kCamposRmc = 9;
/// Até o HDOP, que é o índice 7. A altitude e a separação geoidal vêm
/// depois e não interessam a este aparelho.
constexpr std::size_t kCamposGga = 8;

/// Dois dígitos hexadecimais, que é o tamanho do checksum NMEA.
bool dois_hex(const char* p) {
    return valor_hex(p[0]) >= 0 && valor_hex(p[1]) >= 0;
}

/// Valida a moldura de qualquer sentença — `$`, `*`, checksum — e devolve em
/// `asterisco` onde o corpo termina.
ErroNmea delimita(const char* sentenca, std::size_t tamanho,
                  std::size_t* asterisco) {
    if (sentenca == nullptr || tamanho == 0) return ErroNmea::Vazia;

    const std::size_t n = sem_terminador(sentenca, tamanho);
    if (n == 0) return ErroNmea::Vazia;
    if (sentenca[0] != '$') return ErroNmea::SemCifrao;

    const std::size_t ast = posicao_asterisco(sentenca, n);
    if (ast >= n) return ErroNmea::SemAsterisco;
    if (n - ast != 3 || !dois_hex(sentenca + ast + 1)) {
        return ErroNmea::ChecksumMalformado;
    }
    if (!checksum_valido(sentenca, tamanho)) return ErroNmea::ChecksumInvalido;

    *asterisco = ast;
    return ErroNmea::Nenhum;
}

/// O cabeçalho é `$` + 2 do talker + o tipo de três letras + `,`.
///
///   índice  0  1 2  3 4 5  6
///           $  G N  R M C  ,
///
/// O talker não é conferido de propósito — ver a nota no cabeçalho.
bool e_do_tipo(const char* sentenca, std::size_t ast, const char* tipo) {
    if (ast < 7) return false;
    return sentenca[3] == tipo[0] && sentenca[4] == tipo[1] &&
           sentenca[5] == tipo[2] && sentenca[6] == ',';
}

/// Recorta os campos entre vírgulas, do byte 7 até o `*`, e devolve quantos
/// saíram. Campo vazio é recorte de comprimento zero, e não some — a posição
/// é o que identifica cada campo.
std::size_t fatia_campos(const char* sentenca, std::size_t ast, Campo* campos,
                         std::size_t teto) {
    std::size_t quantos = 0;
    std::size_t inicio = 7;
    for (std::size_t i = 7; i <= ast && quantos < teto; ++i) {
        if (i == ast || sentenca[i] == ',') {
            campos[quantos].ini = sentenca + inicio;
            campos[quantos].n = i - inicio;
            ++quantos;
            inicio = i + 1;
        }
    }
    return quantos;
}

/// O envelope que **toda** sentença NMEA tem, da moldura ao recorte dos
/// campos.
///
/// Extraído porque a RMC e a GGA o repetiam linha por linha, e duas cópias de
/// uma validação de fronteira são duas chances de só uma delas ser corrigida.
/// `erro_tipo` é o que devolver quando o tipo não casa: o `LeitorGps` depende
/// de `NaoEhRmc` para saber que ainda vale tentar a GGA na mesma linha.
ErroNmea recorta_campos(const char* sentenca, std::size_t tamanho,
                        const char* tipo, ErroNmea erro_tipo, Campo* campos,
                        std::size_t quantos_pedidos) {
    std::size_t ast = 0;
    const ErroNmea moldura = delimita(sentenca, tamanho, &ast);
    if (moldura != ErroNmea::Nenhum) return moldura;
    if (!e_do_tipo(sentenca, ast, tipo)) return erro_tipo;
    if (fatia_campos(sentenca, ast, campos, quantos_pedidos) <
        quantos_pedidos) {
        return ErroNmea::CamposDeMenos;
    }
    return ErroNmea::Nenhum;
}

/// Uma coordenada e seu hemisfério, já em graus decimais com sinal.
bool le_coordenada(const Campo& valor, const Campo& hemisferio,
                   std::size_t digitos_de_grau, char negativo, char positivo,
                   double limite, float* destino) {
    double graus = 0.0;
    if (!graus_minutos(valor, digitos_de_grau, &graus)) return false;
    if (graus > limite) return false;
    if (hemisferio.n != 1) return false;
    const char h = hemisferio.ini[0];
    if (h != negativo && h != positivo) return false;
    *destino = static_cast<float>(h == negativo ? -graus : graus);
    return true;
}

/// Velocidade em nós e rumo em graus, os dois campos que descrevem o
/// movimento. Rumo vazio com o veículo parado é normal, não erro.
ErroNmea le_movimento(const Campo& c_nos, const Campo& c_rumo, Telemetria* t) {
    double nos = 0.0;
    if (!decimal(c_nos, &nos) || nos > kMaxNos) {
        return ErroNmea::CampoInvalido;
    }
    t->velocidade_kmh = static_cast<float>(nos * kNoParaKmh);

    if (c_rumo.vazio()) return ErroNmea::Nenhum;

    double rumo = 0.0;
    if (!decimal(c_rumo, &rumo) || rumo >= 360.0) {
        return ErroNmea::CampoInvalido;
    }
    t->rumo_graus = static_cast<float>(rumo);
    t->rumo_valido = true;
    return ErroNmea::Nenhum;
}

/// Dois dígitos, que é como o NMEA escreve hora, minuto, segundo, dia, mês e
/// ano — sempre com o zero à esquerda.
bool dois_digitos(const char* p, std::uint32_t* destino) {
    return inteiro(Campo{p, 2}, destino);
}

/// `hhmmss.sss`. O segundo 60 é o bissexto, que o receptor entrega e o
/// `TempoUtc` aceita; recusá-lo apagaria a data da fix inteira.
bool le_hora(const Campo& c, Telemetria* t) {
    std::uint32_t hh = 0, mm = 0, ss = 0;
    if (c.n < 6 || !dois_digitos(c.ini, &hh) ||
        !dois_digitos(c.ini + 2, &mm) || !dois_digitos(c.ini + 4, &ss)) {
        return false;
    }
    if (hh >= 24 || mm >= 60 || ss > 60) return false;
    t->hora = static_cast<std::uint8_t>(hh);
    t->minuto = static_cast<std::uint8_t>(mm);
    t->segundo = static_cast<std::uint8_t>(ss);
    return true;
}

/// `ddmmyy`. O ano de dois dígitos é do século 21 — o NMEA não diz mais que
/// isso, e este aparelho não existia antes de 2000.
bool le_data(const Campo& c, Telemetria* t) {
    std::uint32_t dd = 0, mo = 0, aa = 0;
    if (c.n != 6 || !dois_digitos(c.ini, &dd) ||
        !dois_digitos(c.ini + 2, &mo) || !dois_digitos(c.ini + 4, &aa)) {
        return false;
    }
    if (dd < 1 || dd > 31 || mo < 1 || mo > 12) return false;
    t->dia = static_cast<std::uint8_t>(dd);
    t->mes = static_cast<std::uint8_t>(mo);
    t->ano = static_cast<std::uint16_t>(2000 + aa);
    return true;
}

}  // namespace

const char* descreve(ErroNmea erro) {
    switch (erro) {
        case ErroNmea::Nenhum:             return "ok";
        case ErroNmea::Vazia:              return "sentenca vazia";
        case ErroNmea::SemCifrao:          return "nao comeca com '$'";
        case ErroNmea::SemAsterisco:       return "sem '*' de checksum";
        case ErroNmea::ChecksumMalformado: return "checksum sem dois digitos hex";
        case ErroNmea::ChecksumInvalido:   return "checksum nao confere";
        case ErroNmea::NaoEhGga:            return "nao e uma GGA";
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
    if (destino == nullptr) return ErroNmea::Vazia;

    // 0=hora 1=status 2=lat 3=N/S 4=lon 5=E/W 6=nos 7=rumo 8=data
    Campo campos[kCamposRmc];
    const ErroNmea envelope = recorta_campos(sentenca, tamanho, "RMC",
                                             ErroNmea::NaoEhRmc, campos,
                                             kCamposRmc);
    if (envelope != ErroNmea::Nenhum) return envelope;

    const Campo& c_status = campos[1];
    if (c_status.n != 1) return ErroNmea::CampoInvalido;
    // Sem fix: sentença íntegra, dado ausente. Não é erro de formato, e não
    // pode sobrescrever a última posição conhecida.
    if (c_status.ini[0] != 'A') return ErroNmea::SemFix;

    Telemetria t;
    if (!le_coordenada(campos[2], campos[3], 2, 'S', 'N', 90.0, &t.lat) ||
        !le_coordenada(campos[4], campos[5], 3, 'W', 'E', 180.0, &t.lon)) {
        return ErroNmea::CampoInvalido;
    }

    const ErroNmea movimento = le_movimento(campos[6], campos[7], &t);
    if (movimento != ErroNmea::Nenhum) return movimento;

    // Hora e data ausentes não invalidam a posição — o relógio é
    // conveniência; a posição é o que protege o motorista. As duas juntas,
    // porque meia data não serve para carimbar coisa nenhuma.
    t.data_valida = le_hora(campos[0], &t) && le_data(campos[8], &t);

    *destino = t;
    return ErroNmea::Nenhum;
}

ErroNmea analisa_gga(const char* sentenca, std::size_t tamanho,
                     QualidadeFix* destino) {
    if (destino == nullptr) return ErroNmea::Vazia;

    // 0=hora 1=lat 2=N/S 3=lon 4=E/W 5=qualidade 6=satelites 7=hdop
    Campo campos[kCamposGga];
    const ErroNmea envelope = recorta_campos(sentenca, tamanho, "GGA",
                                             ErroNmea::NaoEhGga, campos,
                                             kCamposGga);
    if (envelope != ErroNmea::Nenhum) return envelope;

    const Campo& c_qualidade = campos[5];
    if (c_qualidade.vazio()) return ErroNmea::CampoInvalido;
    // `0` é "sem solução": sentença íntegra, dado ausente. Não é erro de
    // formato, e não pode sobrescrever a última qualidade conhecida.
    if (c_qualidade.ini[0] == '0') return ErroNmea::SemFix;

    std::uint32_t satelites = 0;
    if (!inteiro(campos[6], &satelites) || satelites > kMaxSatelites) {
        return ErroNmea::CampoInvalido;
    }

    double hdop = 0.0;
    if (!decimal(campos[7], &hdop) || hdop > kMaxHdop) {
        return ErroNmea::CampoInvalido;
    }

    destino->satelites = static_cast<std::uint8_t>(satelites);
    destino->hdop = static_cast<float>(hdop);
    return ErroNmea::Nenhum;
}

}  // namespace coruja
