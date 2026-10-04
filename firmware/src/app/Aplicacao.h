#pragma once
#include <cstddef>
#include <cstdint>

#include "app/PilotoAlerta.h"
#include "armazenamento/Armazenamento.h"
#include "display/Brilho.h"
#include "display/TelaMenu.h"
#include "display/TelaPrincipal.h"
#include "encoder/Encoder.h"
#include "app/DiarioBordo.h"
#include "menu/MenuAjustes.h"
#include "nucleo/DetectorParado.h"
#include "nucleo/GravadorConfig.h"
#include "nucleo/VerificadorDownload.h"

namespace coruja {

class Logger;

/// Quanto tempo depois do último giro o brilho ajustado fora do menu vai
/// ao cartão.
///
/// **Existe para não gastar um ciclo de escrita por detente.** Um giro
/// decidido do encoder produz vários passos em menos de um segundo; gravar
/// a cada um seriam dezenas de escritas num meio de ciclos finitos, e o
/// valor intermediário não interessa a ninguém. Só o repouso interessa.
constexpr std::uint32_t kEsperaGravacaoBrilhoMs = 5000;

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
              std::size_t capacidade, Visor* visor = nullptr);

    /// Uma volta do laco.
    void passo(std::uint32_t agora_ms);

    /// Forca o redesenho completo na proxima volta.
    ///
    /// Para depois do OTA, que e bloqueante e desenha a tela dele por cima.
    /// As telas so redesenham o que mudou, e o que a tela de atualizacao
    /// deixou no painel nao esta em nenhum instantaneo -- sem isto, a tela
    /// de dirigir voltaria por cima de pedacos dela.
    void invalida_tela();

    /// O que a tela de informacao do menu mostra sobre a base.
    ///
    /// Vem de fora porque quem carrega a base e quem a conhece: a
    /// `Aplicacao` recebe o `PilotoAlerta` ja alimentado e nao tem como
    /// saber a versao nem a contagem. A taxa do GPS, essa sim, ela le do
    /// monitor a cada volta.
    void define_base_carregada(const CabecalhoBase& cabecalho,
                               std::size_t pontos);

    const MenuAjustes& menu() const { return menu_; }
    const DetectorParado& detector() const { return detector_; }

    /// Qual tela esta no ar. Exposto para teste: e a decisao que o laco
    /// toma a cada volta, e ela nao se le olhando os pixels.
    bool mostrando_menu() const { return menu_.aberto(); }

    /// Resultado da ultima tentativa de gravacao, para a tela e o log.
    ResultadoGravacao ultima_gravacao() const { return ultima_gravacao_; }
    unsigned gravacoes() const { return gravacoes_; }

private:
    void aplica_ajustes();
    void executa(AcaoMenu acao);
    void desenha(std::uint32_t agora_ms);

    LeitorGps&     gps_;
    Encoder&       encoder_;
    PilotoAlerta&  piloto_;
    Brilho&        brilho_;
    Armazenamento& cartao_;
    AcoesAplicacao& acoes_;
    Logger&        log_;
    DetectorParado detector_;
    DiarioBordo    diario_;
    MenuAjustes    menu_;
    Visor*         visor_;
    TelaPrincipal  tela_;
    TelaMenu       tela_menu_;
    /// Para detectar a TRANSICAO entre as duas telas, nao o estado.
    bool           menu_no_ar_ = false;
    /// Clique recusado por veiculo em movimento (RF05.1). A tela mostra o
    /// aviso por 2 s; sem ele o clique pareceria nao ter efeito.
    std::uint32_t  aviso_ota_em_ms_ = 0;
    bool           houve_aviso_ota_ = false;
    /// Brilho ajustado fora do menu, esperando o repouso para ser gravado.
    std::uint32_t  brilho_mexido_em_ms_ = 0;
    bool           houve_ajuste_brilho_ = false;
    bool           gravacao_pendente_ = false;
    InfoAparelho   info_;
    std::uint32_t  sem_sinal_desde_ms_ = 0;
    bool           houve_fix_ = false;
    char*          trabalho_;
    std::size_t    capacidade_;
    ResultadoGravacao ultima_gravacao_ = ResultadoGravacao::Gravado;
    unsigned       gravacoes_ = 0;
};

}  // namespace coruja
