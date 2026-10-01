#pragma once
#include <cstddef>
#include <cstdint>

#include "display/Visor.h"
#include "rede/ObservadorOta.h"

namespace coruja {

/// O que a tela de atualização precisa saber.
struct EstadoOta {
    FaseOta     fase = FaseOta::Conectando;
    unsigned    tentativa = 1;
    std::size_t recebidos = 0;
    std::size_t total = 0;      ///< zero até o cabeçalho da base chegar
};

/// Desenha a atualização em curso, reaproveitando as três faixas do §4.1.
///
/// **A faixa inferior vira barra de progresso**, no mesmo lugar e com a
/// mesma geometria da barra de distância. Não é economia de código: é a
/// mesma pergunta em contextos diferentes — *quanto falta* —, e responder no
/// mesmo canto poupa o motorista de aprender um segundo vocabulário.
///
/// O percentual grande no centro, e o nome da fase acima dele. A hierarquia
/// segue a da tela de dirigir: o número que muda é o que fica grande.
///
/// **Sem barra enquanto o total é desconhecido.** Antes do cabeçalho da base
/// não há denominador, e uma barra que enche sozinha sem referência mentiria
/// sobre o andamento.
///
/// **O progresso vai à tela de 5 em 5 por cento.** O painel não tem buffer
/// duplo e cada repintura apaga antes de desenhar; a 1% o rótulo "BAIXANDO"
/// pisca cem vezes num download. O estado recebido continua exato — quem
/// arredonda é o desenho, e para baixo.
class TelaOta {
public:
    int desenha(const EstadoOta& estado, Visor& visor);
    void invalida();

private:
    struct Instantaneo {
        char rotulo[28] = {};
        char valor[8] = {};
        int  barra_pct = -1;
        bool valido = false;
    };

    Instantaneo compoe(const EstadoOta& estado) const;

    Instantaneo anterior_;
};

}  // namespace coruja
