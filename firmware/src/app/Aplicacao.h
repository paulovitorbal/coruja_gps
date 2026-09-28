#pragma once
#include <cstddef>
#include <cstdint>

#include "app/PilotoAlerta.h"
#include "armazenamento/Armazenamento.h"
#include "display/Brilho.h"
#include "encoder/Encoder.h"
#include "menu/MenuAjustes.h"
#include "nucleo/DetectorParado.h"
#include "nucleo/GravadorConfig.h"

namespace coruja {

class Logger;

/// O que a aplicacao manda fazer e nao sabe como (regra 2).
///
/// O OTA suspende o GPS e fala com a rede; o teste de alertas acende o LED
/// e toca o buzzer fora da maquina de zona. Nenhum dos dois cabe aqui
/// dentro sem arrastar rede e hardware para o alvo de host.
class AcoesAplicacao {
public:
    virtual ~AcoesAplicacao() = default;
    virtual void atualiza_base() = 0;
    virtual void testa_alertas() = 0;
};

/// Junta o alerta, o encoder, o menu e o cartao numa volta so.
///
/// E a peca que **liga** -- nao decide nada. O alerta esta no
/// `PilotoAlerta`, a navegacao no `MenuAjustes`, a pre-condicao no
/// `DetectorParado`, a gravacao no `GravadorConfig`, cada um com suite
/// propria. O que se verifica aqui e a fiacao: que o menu so abre parado,
/// que o ajuste chega ao brilho antes de ser gravado, que o alerta nao
/// para enquanto o menu esta aberto.
///
/// **O alerta vem primeiro em toda volta.** Um menu aberto nao pode
/// atrasar a leitura do GPS: o carro esta parado, mas sair andando com o
/// menu na tela e justamente o caso que o RF05.1 previne.
///
/// ⚠️ **O volume do buzzer ainda nao e aplicado.** A interface `Buzzer` e
/// liga/desliga -- o SFM-27 e ativo e nao tem tom a sintetizar -- e
/// controlar volume exige modular o duty do transistor, que e trabalho de
/// porte ainda nao feito. O valor e lido, editado no menu e gravado no
/// cartao; so nao chega ao alto-falante.
class Aplicacao {
public:
    Aplicacao(LeitorGps& gps, Encoder& encoder, PilotoAlerta& piloto,
              Brilho& brilho, Armazenamento& cartao, AcoesAplicacao& acoes,
              Logger& log, const Configuracao& inicial, char* trabalho,
              std::size_t capacidade);

    /// Uma volta do laco.
    void passo(std::uint32_t agora_ms);

    const MenuAjustes& menu() const { return menu_; }
    const DetectorParado& detector() const { return detector_; }

    /// Resultado da ultima tentativa de gravacao, para a tela e o log.
    ResultadoGravacao ultima_gravacao() const { return ultima_gravacao_; }
    unsigned gravacoes() const { return gravacoes_; }

private:
    void aplica_ajustes();
    void executa(AcaoMenu acao);

    LeitorGps&     gps_;
    Encoder&       encoder_;
    PilotoAlerta&  piloto_;
    Brilho&        brilho_;
    Armazenamento& cartao_;
    AcoesAplicacao& acoes_;
    Logger&        log_;
    DetectorParado detector_;
    MenuAjustes    menu_;
    char*          trabalho_;
    std::size_t    capacidade_;
    ResultadoGravacao ultima_gravacao_ = ResultadoGravacao::Gravado;
    unsigned       gravacoes_ = 0;
};

}  // namespace coruja
