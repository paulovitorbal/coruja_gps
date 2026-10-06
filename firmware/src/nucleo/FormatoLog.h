#pragma once
#include <cstddef>

#include "nucleo/AcumuladorViagem.h"
#include "nucleo/DetectorInfracao.h"

namespace coruja {

/// As duas linhas de log que o aparelho grava no cartão.
///
/// ## Convenções, iguais nos dois arquivos
///
/// **Tudo em UTC, com `Z` explícito.** É o que o GPS entrega, e converter
/// para o fuso local criaria uma segunda verdade para o mesmo instante. O
/// `Z` no fim deixa o arquivo se explicar sozinho — inclusive para quem o
/// abrir meses depois sem lembrar desta decisão.
///
/// **Separador `;` e ponto decimal.** O ponto evita ambiguidade em qualquer
/// linguagem; o ponto e vírgula evita conflito com ele. No Excel brasileiro
/// o arquivo precisa ser *importado* em vez de aberto direto, que é o preço
/// de não ter formato ambíguo.
///
/// **Cabeçalho com versão.** O formato vai mudar; sem a versão na primeira
/// linha, descobrir qual é qual depois exige adivinhar pelas colunas.
///
/// **Coordenadas com CINCO casas decimais, não seis.** Duas razões que
/// apontam para o mesmo número:
///
/// 1. `float` tem ~7 dígitos significativos, e uma latitude de dois dígitos
///    com seis decimais pede oito. A sexta casa é ruído do próprio tipo —
///    `-19.799916` vira `-19.799915` ao passar por um `float`.
/// 2. A base guarda coordenadas escaladas por **1e5** (`formato_dados.md`
///    §0), ou seja cinco casas. A coordenada do radar vem literalmente de um
///    inteiro de cinco casas; imprimir seis inventaria um dígito.
///
/// Cinco casas são 1,11 m de resolução, bem abaixo do erro do próprio GPS.
///
/// As funções devolvem o número de bytes escritos, ou **zero se não coube**
/// — nunca truncam. Linha truncada num CSV é pior que linha ausente, porque
/// o analisador a aceita e produz número errado.

constexpr const char* kCabecalhoInfracoes =
    "# coruja_gps infracoes v1\n"
    "utc;lat;lon;rumo;v_radar;v_max;v_infra;limite;radar_lat;radar_lon;dist_min\n";

/// ⚠️ **v2, e a mudança não está nas colunas.** Elas são as mesmas; o que
/// mudou foi o significado do carimbo. Na v1 o segundo era sempre `00`,
/// porque a linha descrevia um minuto inteiro; na v2 ele é o início da fatia
/// de seis segundos. Um leitor de v1 interpreta um arquivo v2 sem errar
/// nada — mas quem for comparar taxas entre arquivos precisa saber qual é
/// qual, e sem a versão na primeira linha teria de adivinhar pelos
/// intervalos.
constexpr const char* kCabecalhoViagem =
    "# coruja_gps viagem v2\n"
    "utc;lat;lon;v_media;dist_km\n";

/// Buffers mínimos. Dimensionados pelo pior caso de cada campo, com folga.
constexpr std::size_t kTamLinhaInfracao = 128;
constexpr std::size_t kTamLinhaViagem = 80;

std::size_t formata_infracao(const RegistroInfracao& r, char* destino,
                             std::size_t capacidade);

std::size_t formata_ponto_viagem(const PontoViagem& p, char* destino,
                                 std::size_t capacidade);

}  // namespace coruja
