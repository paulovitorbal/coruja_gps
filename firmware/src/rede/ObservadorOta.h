#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Tentativas de download, e o intervalo entre elas (RF05.2).
///
/// **Vivem aqui, e não junto do orquestrador, porque são observáveis.** A
/// tela mostra "BAIXANDO 2/3", e para isso quem desenha precisa do 3 — e
/// não deve incluir o orquestrador inteiro, com `Conexao` e `Baixador`
/// atrás, só para ler uma constante.
constexpr unsigned kTentativas = 3;
constexpr std::uint32_t kEsperaEntreTentativasMs = 5000;

/// Em que ponto da atualização o aparelho está.
///
/// **Existe porque o OTA é bloqueante e demorado.** O `executa()` pode levar
/// dezenas de segundos — conectar no Wi-Fi, consultar, baixar 214 KB, gravar
/// no cartão — e durante todo esse tempo o laço principal não roda. Sem
/// alguém avisando de dentro, a tela congelaria no último quadro e o usuário
/// não teria como saber se o aparelho está trabalhando ou travado.
///
/// A ordem é a da execução, e cada fase é visível ao usuário porque cada uma
/// falha por motivo diferente: sem rede é problema de senha ou de alcance,
/// falha ao consultar é servidor fora, falha ao baixar é conexão instável, e
/// base recusada é arquivo corrompido. Um "erro na atualização" genérico
/// obrigaria a abrir o log para saber o que tentar.
enum class FaseOta : std::uint8_t {
    Conectando,
    Consultando,
    JaEmDia,       ///< versão do servidor é a que já se tem; fim feliz
    Baixando,
    Verificando,
    Gravando,      ///< a troca atômica do RF05.2
    Concluida,
    Falhou,
};

const char* descreve(FaseOta fase);

/// Quem acompanha a atualização de fora.
///
/// **Regra 2 aplicada ao tempo, não ao hardware.** O `AtualizadorOta` decide
/// o que fazer e não sabe desenhar nem acender LED; quem implementa isto sabe
/// desenhar e não sabe nada de HTTP. É o mesmo motivo de o orquestrador
/// receber `Conexao` e `Baixador` em vez de Wi-Fi e sockets.
class ObservadorOta {
public:
    virtual ~ObservadorOta() = default;

    /// Mudou de fase. `tentativa` vai de 1 a `kTentativas` durante o
    /// download — as três tentativas do RF05.2 — e é 1 nas outras fases.
    virtual void fase(FaseOta fase, unsigned tentativa) = 0;

    /// Bytes de corpo recebidos e o total esperado.
    ///
    /// `total` é zero até o cabeçalho da base chegar: ele é que diz quantos
    /// pontos vêm, e antes dele não há como saber o tamanho. Quem desenha
    /// precisa tratar esse caso — uma barra de progresso sem denominador é
    /// uma barra que não se pode desenhar.
    virtual void progresso(std::size_t recebidos, std::size_t total) = 0;
};

}  // namespace coruja
