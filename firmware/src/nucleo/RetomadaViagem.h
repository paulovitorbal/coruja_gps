#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/Nmea.h"

namespace coruja {

/// O que fica no cartão entre uma energização e outra.
///
/// **Existe porque a alimentação é pós-chave:** desligar o carro corta a
/// energia sem aviso, e sem isto cada parada no posto exigiria lembrar de
/// reiniciar a gravação.
struct EstadoViagemSalvo {
    bool ativa = false;
    /// Carimbo do último ponto gravado, em UTC. É o que mede a parada.
    std::uint16_t ano = 0;
    std::uint8_t  mes = 0, dia = 0, hora = 0, minuto = 0;
    /// Distância acumulada até ali, para o trecho seguinte continuar dela.
    float dist_km = 0.0F;
};

constexpr const char* kArquivoEstadoViagem = "viagem.est";

/// Janela de retomada. Parada mais curta que isto continua a mesma viagem;
/// mais longa começa outra.
///
/// **Duas horas, e o caso que define o número é o almoço de estrada**: parar
/// para comer e voltar deve continuar contando os mesmos quilômetros. Dormir
/// e sair no dia seguinte, não.
///
/// O mesmo limite governa as duas coisas — retomar a gravação e continuar a
/// distância —, porque são a mesma pergunta: "isto ainda é a mesma viagem?".
constexpr std::uint32_t kJanelaRetomadaMin = 120;

/// Minutos desde 1970-01-01T00:00Z.
///
/// Sem RTC, a única régua de tempo absoluto é o relógio do GPS, e comparar
/// duas datas civis exige convertê-las a um escalar. Atravessa virada de
/// dia, de mês, de ano e ano bissexto — que é exatamente onde uma subtração
/// campo a campo erraria.
std::int32_t minutos_utc(std::uint16_t ano, std::uint8_t mes, std::uint8_t dia,
                         std::uint8_t hora, std::uint8_t minuto);

/// A viagem salva continua, dada a hora do primeiro fix desta energização?
///
/// Falso se não havia viagem ativa, se a data salva é inválida, ou se a
/// parada passou de `kJanelaRetomadaMin`. **Também falso se o relógio andou
/// para trás** — isso indica dado corrompido, e retomar sobre dado duvidoso
/// é pior que começar limpo.
bool pode_retomar(const EstadoViagemSalvo& salvo, const Telemetria& agora);

/// Serializa em `chave=valor`, legível a olho no cartão.
std::size_t formata_estado(const EstadoViagemSalvo& e, char* destino,
                           std::size_t capacidade);

/// Lê o que `formata_estado` escreveu. Devolve falso em qualquer suspeita —
/// campo faltando, número ilegível, data fora de faixa. Estado meio lido é
/// pior que estado nenhum, porque faz o aparelho retomar com distância
/// errada.
bool analisa_estado(const char* texto, std::size_t tamanho,
                    EstadoViagemSalvo* destino);

constexpr std::size_t kTamEstadoViagem = 96;

}  // namespace coruja
