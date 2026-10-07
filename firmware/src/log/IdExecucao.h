#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Quantos caracteres identificam uma execução.
///
/// Oito do alfabeto abaixo dão 32^8 — perto de um trilhão. A pergunta que
/// isto responde não é "duas execuções podem colidir alguma vez", é "duas
/// execuções VIZINHAS no mesmo arquivo de log colidem". Para isso, oito
/// sobra; e cada caractere a mais é um caractere a mais em toda linha.
constexpr std::size_t kTamIdExecucao = 8;

/// Alfabeto do identificador: dígitos e letras, **sem `0`, `1`, `I`, `O` e
/// `l`**.
///
/// Não é preciosismo: o identificador existe para alguém **ler no log e
/// comparar com outro**, muitas vezes de olho ou dizendo em voz alta ao
/// pedir ajuda. `l1` e `O0` são onde a leitura erra, e um identificador lido
/// errado manda procurar na execução errada.
constexpr const char kAlfabetoId[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

/// Monta o identificador a partir de um número aleatório.
///
/// `destino` recebe `kTamIdExecucao` caracteres mais o terminador — portanto
/// precisa de `kTamIdExecucao + 1` bytes.
///
/// Separado de quem gera a aleatoriedade de propósito: o sorteio vem do TRNG
/// no RP2350 e não existe no host, mas a conversão de bits em caracteres é
/// pura — e é nela que mora o erro possível (alfabeto, tamanho, terminador).
void monta_id_execucao(std::uint64_t sorteio, char* destino);

/// O identificador desta execução. Vazio até alguém chamar `define_id_execucao`.
///
/// ⚠️ **É global, e isso é deliberado.** O `Logger` é uma interface com
/// assinatura de três argumentos, usada em centenas de lugares; acrescentar
/// o identificador a ela faria toda chamada carregar um valor que ninguém
/// decide e que é o mesmo para o aparelho inteiro. Ele pertence à execução,
/// não à mensagem.
const char* id_execucao();

/// Fixa o identificador desta execução. Chamado uma vez, no boot.
void define_id_execucao(std::uint64_t sorteio);

/// O separador entre o identificador e o resto da linha: um espaço, ou nada
/// quando ainda não há identificador.
///
/// Existe para que as linhas gravadas ANTES do boot sortear — e as do alvo
/// de host, que não sorteia — não comecem com um espaço solto. Linha que
/// começa com espaço atrapalha quem alinha o arquivo de olho, e é o tipo de
/// sujeira que ninguém conserta depois porque ninguém sabe de onde veio.
const char* separador_execucao();

/// Esquece o identificador, voltando ao estado de antes do boot.
///
/// ⚠️ **Só a suíte usa.** O aparelho sorteia uma vez e nunca esquece — é o
/// que torna o identificador útil. Existe porque o estado "ainda não
/// sorteou" é um caso real do arranque, e testá-lo exige poder voltar a ele;
/// sem isto o teste dependeria da ordem em que a suíte roda.
void esquece_id_execucao();

}  // namespace coruja
