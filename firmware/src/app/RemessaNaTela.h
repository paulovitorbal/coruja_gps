#pragma once
#include <cstdint>

#include "app/OtaNaTela.h"
#include "rede/RemessaDados.h"

namespace coruja {

/// Mostra a remessa de dados na tela e no LED, de dentro dela.
///
/// **Não desenha nada por conta própria.** Traduz `FaseRemessa` para o que a
/// `TelaOta` já sabe mostrar e delega. A tela foi feita para a atualização,
/// mas a pergunta que ela responde — *em que pé está, e quanto falta* — é a
/// mesma, e a geometria também: rótulo, percentual grande, barra embaixo.
///
/// Duplicá-la para trocar duas palavras custaria um segundo arquivo de
/// desenho para manter em sincronia com o §4.1, e foi por isso que a
/// `EstadoOta` ganhou um rótulo opcional em vez de a tela ganhar um irmão.
///
/// O que aparece no rótulo é **o nome do arquivo da vez**, e não o da fase:
/// numa remessa de oito arquivos, "enviando" sozinho fica parado meio minuto
/// e não diz se o aparelho anda. `3/8 coruja.log` diz.
class RemessaNaTela final : public ObservadorRemessa, public Batimento {
public:
    RemessaNaTela(Visor& visor, LedRgb& led, Pausa& relogio)
        : ponte_(visor, led, relogio) {}

    void fase(FaseRemessa fase, const char* nome, unsigned indice,
              unsigned total) override;
    void progresso(std::size_t enviados, std::size_t total) override;

    /// Falhou, e agora se sabe por quê. Mesmo papel do `OtaNaTela::falhou`.
    void falhou(const char* motivo);

    void mantem() override { ponte_.mantem(); }

private:
    OtaNaTela ponte_;
    /// O rótulo precisa sobreviver à chamada: a `EstadoOta` guarda o
    /// ponteiro e a tela só o copia no desenho seguinte.
    char      rotulo_[28] = {};
};

/// Traduz a fase da remessa para a que a tela e o LED entendem.
///
/// Exposta para ser testável: é uma tabela, e tabela escrita à mão erra.
FaseOta fase_equivalente(FaseRemessa f);

}  // namespace coruja
