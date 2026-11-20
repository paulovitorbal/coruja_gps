#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/Configuracao.h"

namespace coruja {

class Logger;

/// Onde a varredura de Wi-Fi deposita o que achou — para que isso **nunca**
/// caia na pilha, e para que a escolha da rede tenha teste.
///
/// Irmã da `CaixaDns`, e pelo mesmo motivo: **não há como cancelar uma
/// varredura**. O `cyw43_wifi_scan` guarda a função e o `env`, e o driver
/// segue chamando enquanto o rádio varre. Quando o prazo estoura, quem
/// esperava vai embora — e até 17/11/2026 o `env` era o endereço de uma
/// estrutura local de `conecta()`: um quadro de pilha que, na volta,
/// pertence a outra coisa. A escrita atrasada corrompe o que estiver ali.
///
/// A caixa vive pelo programa inteiro e o `env` deixa de ser ponteiro: passa
/// a ser um **número de geração**. A chamada atrasada chega, não reconhece a
/// geração e vai embora sem tocar em nada.
///
/// Vive aqui, e não dentro do `RedeWifi`, porque ele só compila para o
/// RP2350: enterrada lá, esta seria mais uma guarda de memória sem teste —
/// e junto com ela ia a regra de escolha de rede do ADR 0002, que nunca teve
/// um.
class CaixaVarredura {
public:
    /// `ultima_usada` existe para o teste: a volta do contador acontece
    /// depois de 2³² varreduras, e sem poder começar perto da borda a
    /// garantia de "nunca devolve zero" ficaria escrita e não verificada.
    explicit CaixaVarredura(std::uint32_t ultima_usada = 0)
        : ultima_(ultima_usada) {}

    /// Abre uma varredura e devolve a geração que a identifica.
    ///
    /// `cfg` e `log` precisam viver enquanto a varredura estiver aberta —
    /// e é exatamente o que `abandona()` garante: depois dele a caixa não
    /// toca em nenhum dos dois.
    ///
    /// Nunca devolve zero, e nunca repete a geração de uma varredura
    /// anterior.
    std::uint32_t abre(const Configuracao& cfg, Logger& log);

    /// A varredura deixou de interessar (prazo estourado, ou terminou).
    ///
    /// Não cancela nada no driver, porque não há como: só faz a chamada que
    /// ainda vier não encontrar ninguém.
    void abandona();

    /// Uma rede que a varredura enxergou. Ignorada quando a geração não é a
    /// da varredura em curso — é assim que a chamada atrasada não escreve.
    ///
    /// **Guarda a MENOR posição na lista**, e não a primeira que casar: a
    /// varredura devolve os pontos de acesso em ordem arbitrária, e o que
    /// vale é a prioridade do `coruja.cfg` (ADR 0002).
    void viu(std::uint32_t geracao, const char* ssid, int rssi,
             unsigned canal);

    bool        tem_escolhida() const { return escolhida_ < kMaxRedes; }
    /// Só faz sentido com `tem_escolhida()`: a posição dela na lista.
    std::size_t escolhida() const { return escolhida_; }
    std::size_t vistas() const { return vistas_; }
    /// Sinal e canal **da escolhida**, para a linha única do desfecho.
    int         rssi() const { return rssi_; }
    unsigned    canal() const { return canal_; }

private:
    /// Zero quer dizer "nenhuma varredura em curso", e por isso `abre` pula
    /// o zero ao dar a volta.
    std::uint32_t       em_curso_ = 0;
    std::uint32_t       ultima_ = 0;
    const Configuracao* cfg_ = nullptr;
    Logger*             log_ = nullptr;
    std::size_t         escolhida_ = kMaxRedes;
    std::size_t         vistas_ = 0;
    int                 rssi_ = 0;
    unsigned            canal_ = 0;
};

}  // namespace coruja
