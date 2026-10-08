#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/Nmea.h"

namespace coruja {

enum class EstadoViagem : std::uint8_t {
    Parada,      ///< não está gravando
    Aguardando,  ///< clicou iniciar sem fix — o arquivo nasce no primeiro
    Gravando,
};

const char* descreve(EstadoViagem e);

/// Quantos segundos cada linha do registro descreve.
///
/// Dez amostras por minuto, por decisão do autor em 2026-10-06. Antes era
/// uma, e a conta que condenou aquela taxa é direta:
///
///     120 km/h = 2 km por MINUTO
///
/// Um vértice a cada dois quilômetros não descreve trajeto nenhum — numa via
/// expressa o traçado vira uma reta entre pontos que ignoram as curvas, as
/// alças e os retornos por onde o carro de fato passou. Serve para somar
/// quilômetros; não serve para ver por onde se andou.
///
/// A 6 s, a mesma velocidade dá **200 m** entre vértices. Em cidade, a 50
/// km/h, dá 83 m.
///
/// Seis divide sessenta, e é isso que mantém os carimbos **redondos**: as
/// fatias caem em `:00, :06, :12 … :54`, e não numa janela deslizante que
/// começa onde o clique calhou.
///
/// ⚠️ O custo é dez vezes mais escrita no cartão. Uma viagem de uma hora
/// passa de 60 para 600 linhas (~27 KB). O tamanho não é o problema — o
/// número de operações é: cada ponto **monta o volume, abre, acrescenta,
/// fecha e desmonta** (ver `CartaoSd::acrescenta_arquivo`), e isso passa de
/// uma para dez vezes por minuto. São dez atualizações de diretório por
/// minuto no mesmo setor de FAT, e cartão tem ciclos finitos.
///
/// Aceito com conhecimento de causa: o aparelho é de estudo, o cartão é
/// barato e substituível, e um trajeto que não dá para ver não serve para
/// nada. Se virar problema, o conserto é acumular N pontos em RAM e gravar
/// em lote — o que troca desgaste por perder o último lote num corte de
/// energia.
constexpr std::uint8_t kSegundosPorAmostra = 6;

/// Uma fatia de viagem, pronta para virar linha.
///
/// O carimbo identifica a **fatia descrita** — `:00`, `:06`, `:12`… —, não um
/// instante dentro dela. Posição e distância são as do **fim** da fatia; a
/// velocidade é a média dela.
/// O que o alerta sabia sobre radares, no instante de uma leitura.
///
/// Existe por causa do Eixão. Em 07/10/2026 o aparelho mostrou radares de
/// 60 km/h a quem dirigia a 80 na pista PRINCIPAL, porque a pista lateral
/// corre a poucos metros e seus radares entram na mesma janela de 300 m. A
/// precedência do RF03.4 manda vencer a situação mais grave, e 80 contra um
/// limite de 60 é Perigo enquanto 80 contra 80 é Conforme — então o radar da
/// outra pista ganha sempre.
///
/// Sem registrar isto, a única evidência é a lembrança de quem dirigiu.
///
/// ⚠️ **São DOIS radares, e é de propósito.** O `alerta` é o que a tela
/// mostrou (venceu por gravidade); o `proximo` é o fisicamente mais perto.
/// Quando eles divergem, a divergência É o diagnóstico — e um campo só não
/// conseguiria mostrá-la.
struct RadarDaAmostra {
    /// A zona mais grave vista na fatia, como número — a ordem do `Zona` é a
    /// gravidade crescente, então o maior é o pior.
    ///
    /// ⚠️ **É isto que mede o incômodo**, e o incômodo é o problema. A queixa
    /// do Eixão não foi "atribuiu ao radar errado": foi o buzzer tocando quase
    /// o tempo todo. Esta coluna responde em número "que fração do trajeto
    /// esteve em Perigo" — antes do filtro, e depois dele.
    std::uint8_t zona_pior = 0;

    /// Quantos pontos disputaram, no pior instante da fatia. Ver
    /// `Veredito::n_candidatos`.
    std::uint8_t n_candidatos = 0;

    bool         tem_alerta = false;
    float        dist_alerta_m = 0.0F;
    std::uint8_t limite_alerta = 0;

    bool         tem_proximo = false;
    float        dist_proximo_m = 0.0F;
    std::uint8_t limite_proximo = 0;
};

struct PontoViagem {
    std::uint16_t ano = 0;
    std::uint8_t  mes = 0, dia = 0, hora = 0, minuto = 0, segundo = 0;
    float lat = 0.0F;
    float lon = 0.0F;
    float v_media_kmh = 0.0F;
    float dist_km = 0.0F;
    /// O rumo do veículo, em graus do norte, na última leitura da fatia —
    /// a mesma leitura de que vêm `lat` e `lon`.
    ///
    /// ⚠️ **Vem do receptor, e isso é o ponto.** O NEO-M8N deriva o curso por
    /// Doppler, não por diferença de posições, e acerta cerca de 1° com o
    /// veículo em movimento. Derivar o rumo de duas amostras do log, a 13 m
    /// uma da outra com 3 m de ruído, dá ~18° de erro — que a 200 m viram 60 m
    /// de erro na distância perpendicular até um radar, mais que a própria
    /// separação entre as pistas que se quer medir. Medido em 08/10/2026.
    float rumo_graus = 0.0F;

    /// A RMC vem sem rumo com o veículo parado. Sem esta bandeira, zero seria
    /// lido como "apontando para o norte".
    bool  rumo_valido = false;

    /// A **menor** distância vista na fatia, e o radar dela.
    ///
    /// Menor, e não a última leitura: a 80 km/h o carro anda 133 m nos seis
    /// segundos da fatia, e uma leitura instantânea erraria o ponto de maior
    /// aproximação justamente onde ele importa. É a mesma convenção do
    /// `v_media_kmh`, que já descreve a fatia inteira e não um instante.
    RadarDaAmostra radar{};
};

enum class EventoViagem : std::uint8_t {
    Nada,
    Abre,     ///< criar o arquivo — nome em `nome_arquivo()`
    Grava,    ///< gravar um ponto — dados em `ultimo_ponto()`
    Encerra,  ///< encerramento automático por veículo parado
};

/// Tempo contínuo abaixo de `kVelocidadeParadoKmh` que encerra a viagem
/// sozinha. Existe para o esquecimento: sem ele, "iniciar" valeria até alguém
/// lembrar de parar, e o aparelho registraria o carro na garagem.
constexpr std::uint32_t kParadoEncerraViagemMs = 5U * 60U * 1000U;

/// Passo máximo que entra na integração de distância.
///
/// Sem o teto, o primeiro fix depois de um túnel somaria `velocidade × tempo
/// do túnel` de uma só vez — quilômetros que não existiram. Com fix contínuo
/// a 4 Hz os passos são de 250 ms, então 2 s é folga de oito vezes sobre o
/// normal e corta qualquer buraco real.
constexpr std::uint32_t kMaxPassoIntegracaoMs = 2000;

/// Acumula uma viagem: dez pontos por minuto, com média e distância.
///
/// ## Três decisões que não são óbvias
///
/// **A distância vem da integração da velocidade, não da soma das distâncias
/// entre coordenadas.** No NEO-M8N a velocidade vem de Doppler, que é muito
/// menos ruidosa que diferenciar posições. Somando coordenadas, o jitter
/// parado acumula distância fantasma — num semáforo de dois minutos o
/// aparelho "andaria" dezenas de metros sem sair do lugar.
///
/// **O ponto sai na virada da fatia UTC, não a cada N segundos desde o
/// clique.** Os carimbos saem redondos, e a média passa a ser exatamente "de
/// `:06` a `:11`" em vez de uma janela deslizante sem significado.
///
/// **Fatia sem fix não produz linha.** Não há coordenada para gravar, e
/// inventar a última conhecida poria no arquivo posição que o aparelho não
/// mediu. O buraco no horário é o registro de que houve buraco.
///
/// ⚠️ A fatia em curso quando a viagem termina **não é gravada** — faltam
/// segundos para ela fechar. É perda de no máximo 5 s de trajeto, e vale
/// para os dois fins: o clique em "parar" e o corte de energia. A distância
/// acumulada continua em `dist_km()`, que é o que vai ao arquivo de estado.
class AcumuladorViagem {
public:
    /// `dist_inicial_km` retoma a distância de um trecho anterior; zero
    /// começa viagem nova.
    void inicia(float dist_inicial_km = 0.0F);
    void para();

    EstadoViagem estado() const { return estado_; }
    bool ativa() const { return estado_ != EstadoViagem::Parada; }

    EventoViagem alimenta(const Telemetria& t, const RadarDaAmostra& radar,
                          bool tem_fix,
                          std::uint32_t agora_ms);

    /// Válido depois de `Abre`. `AAAAMMDD_HHMMSS.log`, em UTC.
    const char* nome_arquivo() const { return nome_; }
    /// Válido depois de `Grava`.
    const PontoViagem& ultimo_ponto() const { return ponto_; }

    float dist_km() const { return dist_km_; }

private:
    void monta_nome(const Telemetria& t);
    void abre_amostra(const Telemetria& t);
    /// Guarda a leitura se ela for mais perto que a melhor desta fatia.
    void considera_radar(const RadarDaAmostra& r);
    void fecha_amostra();
    /// Qual fatia de `kSegundosPorAmostra` segundos este instante ocupa.
    static std::uint8_t fatia_de(const Telemetria& t);

    EstadoViagem estado_ = EstadoViagem::Parada;
    char         nome_[24] = {};

    float        dist_km_ = 0.0F;
    bool         tem_passo_anterior_ = false;
    std::uint32_t anterior_ms_ = 0;

    // Minuto em curso
    bool          amostra_aberta_ = false;
    /// O melhor (= mais perto) que se viu nesta fatia, ainda em construção.
    RadarDaAmostra radar_da_fatia_{};
    float          ultimo_rumo_ = 0.0F;
    bool           ultimo_rumo_valido_ = false;
    std::uint16_t ano_ = 0;
    std::uint8_t  mes_ = 0, dia_ = 0, hora_ = 0, minuto_ = 0;
    /// A fatia que esta aberta, de 0 a 59/kSegundosPorAmostra-1.
    std::uint8_t  fatia_ = 0;
    float         soma_vel_ = 0.0F;
    std::uint32_t amostras_ = 0;
    float         ultima_lat_ = 0.0F;
    float         ultima_lon_ = 0.0F;

    // Encerramento automático
    bool          contando_parado_ = false;
    std::uint32_t parado_desde_ms_ = 0;

    PontoViagem   ponto_{};
};

}  // namespace coruja
