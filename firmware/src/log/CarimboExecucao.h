#pragma once

namespace coruja {

/// De onde o log tira o carimbo de tempo de cada linha.
///
/// Devolve um texto já pronto — a formatação é do `formata_carimbo`, que mora
/// no núcleo e é testada no host. Aqui só mora a ligação.
///
/// ⚠️ **Por que um gancho, e não um parâmetro do `LoggerCartao`.** A hora vem
/// do relógio do RP2350 e do `hora_utc()`, que só existem no alvo; o logger
/// compila no host e não pode saber disso. E é o mesmo motivo do
/// `id_execucao()` ser global: o carimbo pertence ao MOMENTO, não à mensagem,
/// e passá-lo em toda chamada faria centenas de pontos carregarem um valor
/// que nenhum deles decide.
using FonteDeCarimbo = const char* (*)();

/// Instala a fonte. Chamada uma vez, no boot. `nullptr` desliga o carimbo.
void define_fonte_de_carimbo(FonteDeCarimbo fonte);

/// O carimbo de agora. Vazio quando não há fonte instalada — que é o caso do
/// host e o das linhas gravadas antes de o boot chegar nesta parte.
const char* carimbo_agora();

/// O separador entre o carimbo e o resto da linha: um espaço, ou nada quando
/// não há carimbo.
///
/// Mesmo cuidado do `separador_execucao()`: linha começando com espaço solto
/// é sujeira que ninguém conserta depois porque ninguém sabe de onde veio.
const char* separador_carimbo();

/// Esquece a fonte. **Só a suíte usa**, pelo mesmo motivo do
/// `esquece_id_execucao()`: "ainda não instalou" é um estado real do arranque
/// e testá-lo exige poder voltar a ele.
void esquece_fonte_de_carimbo();

}  // namespace coruja
