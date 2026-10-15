#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Em que velocidade o módulo respondeu da última vez.
///
/// **Existe porque a sondagem custa ~1,7 s em toda partida morna.** O
/// `CFG-CFG` grava 115200 na BBR do receptor, e a bateria de reserva a
/// mantém por horas: no religamento o módulo já fala 115200 enquanto nós
/// começamos pedindo a 9600, levamos duas recusas por silêncio e só então
/// sondamos. Medido em 15/10/2026: o módulo fixou em 0,4 s e quem dirigia
/// esperou 5,9 s, com 1,2 s gastos exatamente nessas duas tentativas.
///
/// ⚠️ **Não é configuração, é lembrança.** O arquivo some com o cartão e o
/// aparelho volta a sondar do começo — perde-se tempo, nunca correção. Por
/// isso ele não entra no `coruja.cfg`, que é escrito à mão.
constexpr const char* kArquivoBaudGps = "gps.est";

/// Cabe o `baud=115200\n` com folga.
constexpr std::size_t kTamBaudSalvo = 32;

/// Escreve `baud=<n>\n`. Devolve o tamanho, ou zero se não coubesse.
std::size_t formata_baud(std::uint32_t baud, char* destino,
                         std::size_t capacidade);

/// Lê o que `formata_baud` escreveu.
///
/// **Só os dois bauds que este aparelho usa são aceitos.** Qualquer outro
/// número é dado estragado, e obedecê-lo abriria a porta numa velocidade em
/// que ninguém fala — justamente a falha de 08/10/2026, agora por outro
/// caminho. Em caso de dúvida, devolve falso e quem chamou sonda do começo.
bool analisa_baud(const char* texto, std::size_t tamanho,
                  std::uint32_t* destino);

}  // namespace coruja
