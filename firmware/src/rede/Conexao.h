#pragma once

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

/// `inline` pelo mesmo motivo do `descreve(ErroCartao)`: o tipo é portável, a
/// implementação do rádio não.
inline const char* descreve(ErroWifi erro) {
    switch (erro) {
        case ErroWifi::Nenhum:                 return "ok";
        case ErroWifi::ChipNaoIniciou:         return "chip CYW43 nao iniciou";
        case ErroWifi::VarreduraFalhou:        return "varredura falhou";
        case ErroWifi::NenhumaRedeConfigurada: return "nenhuma rede na configuracao";
        case ErroWifi::NenhumaRedeVisivel:
            return "nenhuma rede da lista esta visivel";
        case ErroWifi::FalhaDeAssociacao:
            return "associacao recusada (senha? sinal?)";
    }
    return "erro desconhecido";
}

/// Conexão de rede, vista por quem só precisa subir e descer o enlace.
///
/// Chama-se `Conexao` e não `Rede` porque `Rede` já é a struct de uma rede
/// Wi-Fi configurada, em `Configuracao.h` — são coisas diferentes: uma é o
/// enlace, a outra é uma credencial na lista de prioridade.
///
/// `inicia()` fica de fora: é do porte, roda uma vez no boot, e a lógica de
/// atualização não tem o que decidir a respeito.
class Conexao {
public:
    virtual ~Conexao() = default;

    virtual ErroWifi conecta(const Configuracao& cfg, Logger& log) = 0;
    virtual void desconecta(Logger& log) = 0;
};

}  // namespace coruja
