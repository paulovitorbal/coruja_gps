#pragma once
#include <cstddef>
#include <cstdint>

#include "encoder/EventoEncoder.h"
#include "nucleo/AcumuladorViagem.h"
#include "nucleo/Configuracao.h"
#include "nucleo/PeriodoDoDia.h"

namespace coruja {

/// Fecha sozinho depois deste tempo sem nenhum evento. O menu tapa o
/// velocimetro; deixa-lo aberto por esquecimento e perder a tela.
constexpr std::uint32_t kTimeoutMenuMs = 20000;

/// Passo do volume no menu. A faixa util e 50 a 100 (RF: nunca silencia),
/// e seis posicoes bastam -- janela aberta em 100, ar-condicionado em 50.
constexpr std::uint8_t kPassoVolume = 10;

enum class ItemMenu : std::uint8_t {
    Brilho,         ///< edita o preset do periodo vigente
    ModoNoturno,
    Volume,
    AtualizarBase,
    TestarAlertas,
    Viagem,         ///< inicia ou encerra o registro de trajeto
    Informacao,
    Sair,
};
constexpr std::size_t kItensMenu = 8;

enum class EstadoMenu : std::uint8_t {
    Fechado,
    Navegando,   ///< girar anda pelos itens, clicar entra
    Editando,    ///< girar muda o valor, clicar confirma
    Informando,  ///< tela de leitura; qualquer evento volta
};

/// O que o menu pede a quem o chama. Ele proprio nao toca em nada.
enum class AcaoMenu : std::uint8_t {
    Nenhuma,
    Gravar,          ///< os ajustes mudaram e precisam ir ao cartao
    AtualizarBase,   ///< iniciar o OTA
    TestarAlertas,   ///< acender o LED e tocar o buzzer para conferencia
    AlternarViagem,  ///< iniciar se parada, encerrar se gravando
};

/// O menu de ajustes com o carro parado.
///
/// **Abre ao GIRAR, nao ao clicar.** O clique ja e do OTA (RF05), e o eixo
/// do encoder e facil de pressionar sem intencao -- foi o que motivou a
/// pre-condicao do RF05.1. Girar exige quatro transicoes validas na mesma
/// direcao (RF04), o que um esbarrao nao produz.
///
/// **Andar fecha na hora**, sem gravar pela metade: o que ja foi editado
/// vale, e a gravacao sai junto com o fechamento. A tela do motorista
/// volta sozinha.
///
/// Nao conhece cartao, display nem buzzer: devolve uma `AcaoMenu` e quem
/// chama executa. E o que faz a navegacao inteira rodar no host.
class MenuAjustes {
public:
    explicit MenuAjustes(const Configuracao& inicial) : cfg_(inicial) {}

    /// Uma volta. `parado` vem do `DetectorParado` (RF05.1).
    AcaoMenu avalia(EventoEncoder evento, bool parado, std::uint32_t agora_ms);

    /// Qual preset de brilho o item edita. `Desconhecido` edita o de dia.
    void define_periodo(PeriodoDoDia p) { periodo_ = p; }

    /// O estado da viagem, que o menu exibe mas nao controla.
    ///
    /// O menu pede `AlternarViagem` e quem executa decide o que fazer; o
    /// estado volta por aqui. Mesma razao do brilho externo: o menu mostra,
    /// outro manda.
    void define_estado_viagem(EstadoViagem e) { viagem_ = e; }

    /// Registra um ajuste de brilho feito **fora** do menu.
    ///
    /// O encoder ajusta o brilho com o carro em movimento, quando o menu
    /// nem pode abrir (RF05.1). Esse ajuste tem de chegar aqui, e nao so
    /// ao `Brilho`: a configuracao e a fonte unica de verdade, e quem
    /// chama a reaplica a cada volta -- sem isto o giro seria desfeito no
    /// ciclo seguinte.
    ///
    /// Escreve no preset do periodo vigente, como o item do menu faria.
    /// **Nao marca alteracao:** o `alterado_` governa a gravacao no
    /// fechamento do menu, e este caminho nao tem fechamento -- quem chama
    /// decide quando gravar.
    void registra_brilho_externo(std::uint8_t pct);

    EstadoMenu estado() const { return estado_; }
    ItemMenu item() const { return item_; }
    const Configuracao& ajustes() const { return cfg_; }
    bool aberto() const { return estado_ != EstadoMenu::Fechado; }

    /// Rotulo e valor do item, prontos para a tela.
    const char* rotulo(ItemMenu i) const;
    /// Escreve o valor em `destino`; itens de acao devolvem texto vazio.
    void valor(ItemMenu i, char* destino, std::size_t capacidade) const;

private:
    void abre(std::uint32_t agora_ms);
    AcaoMenu fecha();
    void anda(int passo);
    void edita(int passo);
    std::uint8_t& brilho_vigente();
    std::uint8_t brilho_vigente() const;

    Configuracao cfg_;
    EstadoMenu estado_ = EstadoMenu::Fechado;
    ItemMenu item_ = ItemMenu::Brilho;
    PeriodoDoDia periodo_ = PeriodoDoDia::Desconhecido;
    EstadoViagem viagem_ = EstadoViagem::Parada;
    std::uint32_t ultimo_evento_ms_ = 0;
    bool alterado_ = false;
};

}  // namespace coruja
