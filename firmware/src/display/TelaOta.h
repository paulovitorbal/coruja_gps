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

    /// Por que falhou, curto o bastante para a faixa de texto. Nulo enquanto
    /// não se sabe — o `Falhou` parte de dentro do orquestrador, antes de
    /// alguém ter o resultado na mão.
    ///
    /// Aponta para literal: quem preenche é a composição, com
    /// `descreve_curto(ResultadoOta)`, e a `TelaOta` copia o conteúdo em vez
    /// de guardar o ponteiro.
    const char* motivo = nullptr;

    /// Substitui o nome da fase na linha de rótulo, quando não nulo.
    ///
    /// **Existe para a remessa de dados reaproveitar esta tela.** A geometria
    /// é a mesma e a pergunta é a mesma — *em que pé está* —, mas "BAIXANDO"
    /// durante um envio seria uma palavra errada na tela, e tela que mente é
    /// pior que tela em branco. Aponta para literal; a `TelaOta` copia.
    const char* rotulo = nullptr;

    /// A barra de progresso vale para esta fase. `Baixando` no OTA; a
    /// composição da remessa aponta para a fase de envio.
    ///
    /// Sem isto, a remessa não teria barra nenhuma: a condição estava presa
    /// a `FaseOta::Baixando`, que a remessa nunca produz.
    FaseOta fase_com_barra = FaseOta::Baixando;
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
/// **A falha diz o motivo.** "FALHOU" sozinho manda abrir o log, e ninguém
/// abre log dentro do carro. Sem rede, servidor fora, download interrompido e
/// cartão ruim pedem ações diferentes; o nome da causa é o que escolhe a ação.
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
        char motivo[28] = {};
        int  barra_pct = -1;
        bool valido = false;
    };

    Instantaneo compoe(const EstadoOta& estado) const;

    Instantaneo anterior_;
};

}  // namespace coruja
