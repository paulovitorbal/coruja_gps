#pragma once
#include <cstddef>
#include <cstdint>

#include "nucleo/Configuracao.h"

namespace coruja {

class Logger;

enum class ErroWifi {
    Nenhum,
    ChipNaoIniciou,       ///< cyw43_arch_init falhou
    VarreduraFalhou,
    NenhumaRedeConfigurada,
    NenhumaRedeVisivel,   ///< varreu e nenhuma da lista apareceu
    FalhaDeAssociacao,    ///< visível, mas a conexão não fechou
};

const char* descreve(ErroWifi erro);

/// Rádio Wi-Fi: varre, escolhe e conecta. Só existe no alvo Pico.
///
/// **A escolha é por ordem da lista, não por sinal mais forte** (ADR 0002).
/// O dono do aparelho escreveu as redes numa ordem; respeitá-la torna o
/// comportamento previsível e o log capaz de dizer *por que* escolheu.
///
/// A conexão é **episódica**: sobe no clique, atualiza, e cai. Não há razão
/// para o rádio ficar associado enquanto se dirige — gasta energia, e uma
/// associação de fundo é superfície de ataque num aparelho que ninguém
/// administra.
///
/// > ⚠️ Nenhum método desta classe registra senha em log, nem em caso de
/// > falha de associação. A senha entra, é usada e não sai.
class RedeWifi {
public:
    ~RedeWifi();

    /// Liga o chip. Idempotente; pode ser chamado uma vez no boot.
    ErroWifi inicia(Logger& log);

    /// Varre, escolhe a primeira rede configurada que estiver visível e
    /// conecta. Registra cada rede vista, com canal e RSSI.
    ErroWifi conecta(const Configuracao& cfg, Logger& log);

    /// Desassocia, mantendo o chip ligado para a próxima vez.
    void desconecta(Logger& log);

    bool ligado() const { return iniciado_; }

private:
    bool iniciado_ = false;
    bool associado_ = false;
};

}  // namespace coruja
