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

/// Um minuto de viagem, pronto para virar linha.
///
/// O carimbo identifica o **minuto descrito**, sempre com segundo zero: a
/// linha é o minuto, não um instante dentro dele. Posição e distância são as
/// do **fim** do minuto; a velocidade é a média dele.
struct PontoViagem {
    std::uint16_t ano = 0;
    std::uint8_t  mes = 0, dia = 0, hora = 0, minuto = 0;
    float lat = 0.0F;
    float lon = 0.0F;
    float v_media_kmh = 0.0F;
    float dist_km = 0.0F;
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

/// Acumula uma viagem: um ponto por minuto, com média e distância.
///
/// ## Três decisões que não são óbvias
///
/// **A distância vem da integração da velocidade, não da soma das distâncias
/// entre coordenadas.** No NEO-M8N a velocidade vem de Doppler, que é muito
/// menos ruidosa que diferenciar posições. Somando coordenadas, o jitter
/// parado acumula distância fantasma — num semáforo de dois minutos o
/// aparelho "andaria" dezenas de metros sem sair do lugar.
///
/// **O ponto sai na virada do minuto UTC, não a cada 60 s desde o clique.**
/// Os carimbos saem redondos, e a média passa a ser exatamente "o minuto de
/// `:00` a `:59`" em vez de uma janela deslizante sem significado.
///
/// **Minuto sem fix não produz linha.** Não há coordenada para gravar, e
/// inventar a última conhecida poria no arquivo posição que o aparelho não
/// mediu. O buraco no horário é o registro de que houve buraco.
///
/// ⚠️ O minuto em curso quando a viagem termina **não é gravado** — faltam
/// segundos para ele fechar. É perda de no máximo 59 s de trajeto, e vale
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

    EventoViagem alimenta(const Telemetria& t, bool tem_fix,
                          std::uint32_t agora_ms);

    /// Válido depois de `Abre`. `AAAAMMDD_HHMMSS.log`, em UTC.
    const char* nome_arquivo() const { return nome_; }
    /// Válido depois de `Grava`.
    const PontoViagem& ultimo_ponto() const { return ponto_; }

    float dist_km() const { return dist_km_; }

private:
    void monta_nome(const Telemetria& t);
    void abre_minuto(const Telemetria& t);
    void fecha_minuto();

    EstadoViagem estado_ = EstadoViagem::Parada;
    char         nome_[24] = {};

    float        dist_km_ = 0.0F;
    bool         tem_passo_anterior_ = false;
    std::uint32_t anterior_ms_ = 0;

    // Minuto em curso
    bool          minuto_aberto_ = false;
    std::uint16_t ano_ = 0;
    std::uint8_t  mes_ = 0, dia_ = 0, hora_ = 0, minuto_ = 0;
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
