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
/// ⚠️ **v3: quatro colunas novas, e elas respondem a uma pergunta concreta.**
///
/// Em 07/10/2026 o aparelho mostrou radares de 60 km/h a quem dirigia a 80 na
/// pista PRINCIPAL do Eixão. A pista lateral corre a poucos metros, seus
/// radares caem na mesma janela de 300 m, e a precedência do RF03.4 manda
/// vencer a situação mais grave — 80 contra limite 60 é Perigo, 80 contra 80
/// é Conforme. O radar da outra pista ganha sempre.
///
/// As colunas separam **o que foi alertado** do **que estava mais perto**:
///
///   - `radar_m`, `radar_kmh` — o ponto que a tela mostrou (venceu por
///     gravidade), e o limite dele;
///   - `perto_m`, `perto_kmh` — o ponto fisicamente mais próximo, que em
///     geral NÃO é o mesmo.
///
/// Quando os dois divergem, a divergência é o diagnóstico. Com uma coluna só
/// não haveria como ver isso.
///
/// Vazias quando não havia radar na janela — e vazio é diferente de zero, que
/// é um limite válido (semáforo).
///
/// As colunas novas vão no FIM: um leitor de v2 que corte no quinto campo
/// continua lendo os arquivos novos sem errar.
/// ⚠️ **v3: doze colunas, e `rumo` é a que mais vale.**
///
/// A v3 cresceu em duas etapas no mesmo dia, de nove para doze colunas, e
/// **não virou v4 de propósito**: a v0.2.11, único firmware que grava a forma
/// de nove, nunca foi publicada nem gravada — existe só na máquina do autor.
/// Criar v4 inventaria uma versão que nenhum cartão jamais teve.
///
/// ⚠️ **E `v2` NÃO está livre, apesar de nenhum cartão tê-lo.** As releases
/// v0.2.6 a v0.2.10 estão publicadas e todas gravam `viagem v2` com CINCO
/// colunas. Reusar esse rótulo faria dois formatos incompatíveis
/// compartilharem o mesmo nome — que é precisamente o que um número de versão
/// existe para impedir. Em cartão só existe v1; em firmware publicado, v2
/// existe. Quem decide o rótulo é a segunda coisa.
///
/// Quem lê continua seguro porque o analisador conta colunas por índice e
/// tolera faltar do fim: uma linha de nove e uma de doze passam pelo mesmo
/// caminho.
///
/// `rumo` vem do RECEPTOR, por Doppler, e acerta cerca de 1° em movimento.
/// Sem ele, quem analisa precisa derivar o rumo de duas amostras do log — a
/// 13 m uma da outra, com 3 m de ruído, o que dá ~18° de erro. A 200 m de
/// distância isso vira 60 m de erro na perpendicular até um radar: **mais que
/// a separação entre as pistas que se quer medir.** Medido em 08/10/2026.
///
/// Vazio quando a RMC não traz rumo (veículo parado), e vazio é diferente de
/// `0`, que é norte.
///
/// `zona` é a mais grave da fatia, pela ordem do `Zona` (0 sem sinal, 1
/// segura, 2 conforme, 3 semáforo, 4 margem, 5 perigo). **É esta coluna que
/// mede o incômodo**, e o incômodo é o problema: a queixa do Eixão foi o
/// buzzer tocando quase o tempo todo, não a atribuição errada. Ela responde
/// em número que fração do trajeto esteve em Perigo — antes do filtro de
/// pista e depois dele.
///
/// `n_radares` é quantos pontos passaram por todos os filtros no pior
/// instante da fatia. Diz se havia ambiguidade mesmo quando os limites
/// coincidem — dois radares de 60 em pistas diferentes não aparecem na
/// divergência entre `radar_kmh` e `perto_kmh`, e são ambíguos do mesmo
/// jeito.
constexpr const char* kCabecalhoViagem =
    "# coruja_gps viagem v3\n"
    "utc;lat;lon;v_media;dist_km;radar_m;radar_kmh;perto_m;perto_kmh;"
    "rumo;zona;n_radares\n";

/// Buffers mínimos. Dimensionados pelo pior caso de cada campo, com folga.
constexpr std::size_t kTamLinhaInfracao = 128;
/// 80 cobria a v2. As quatro colunas novas pedem ate ~28 bytes
/// (`;9999.9;255;9999.9;255`), e truncar a linha perderia o campo
/// que foi acrescentado justamente para diagnosticar.
/// 120 cobria as nove colunas. As tres novas pedem ate ~14 bytes
/// (`;359.9;5;255`), e truncar perderia justamente o diagnostico.
constexpr std::size_t kTamLinhaViagem = 144;

std::size_t formata_infracao(const RegistroInfracao& r, char* destino,
                             std::size_t capacidade);

std::size_t formata_ponto_viagem(const PontoViagem& p, char* destino,
                                 std::size_t capacidade);

}  // namespace coruja
