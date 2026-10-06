#pragma once
#include <cstdint>

#include "display/Visor.h"
#include "nucleo/MonitorTaxa.h"
#include "nucleo/Nmea.h"
#include "nucleo/Zonamento.h"

namespace coruja {

/// Fuso fixo em UTC−3. Sem horário de verão: o Brasil o extinguiu em 2019.
constexpr int kFusoHoras = -3;

/// Quanto tempo o aviso de OTA ocupa a faixa inferior.
constexpr std::uint32_t kAvisoOtaMs = 2000;
/// Quanto tempo a barra de brilho ocupa a faixa superior.
constexpr std::uint32_t kBarraBrilhoMs = 1500;

/// ## ⚠️ A tela de dirigir não acumula indicadores
///
/// **Decisão do autor em 2026-10-06:** aqui, quanto menos informação, melhor.
///
/// A pergunta volta sempre que surge um estado novo — "e se a viagem ativa
/// aparecesse num canto?" —, e a resposta é não. Esta é a única tela olhada
/// a 100 km/h, e cada elemento a mais disputa o olhar com a velocidade e com
/// o alerta, que são a razão de o aparelho existir.
///
/// **Estado de funcionalidade pertence ao menu**, que se consulta parado. Foi
/// o caminho da viagem: em vez de um ícone aqui, o item do menu passou a
/// dizer o que o clique vai fazer.
///
/// Acrescentar algo a esta tela é decisão de projeto, não de implementação.
///
/// Tudo que a tela precisa saber. Um struct e não oito parâmetros: a lista
/// cresceria a cada estado novo, e trocar dois `bool` de lugar numa chamada
/// é o tipo de erro que compila.
struct EstadoTela {
    Veredito    veredito;
    Telemetria  telemetria;
    bool        tem_fix = false;
    bool        base_disponivel = true;
    EstadoTaxa  taxa = EstadoTaxa::Nominal;
    /// Desde quando está sem sinal, para o contador `0:14`.
    std::uint32_t sem_sinal_desde_ms = 0;
    /// Instante do clique de OTA recusado por veículo em movimento.
    std::uint32_t aviso_ota_em_ms = 0;
    bool          houve_aviso_ota = false;
    /// Instante do último ajuste de brilho pelo encoder.
    ///
    /// O ajuste direto existe **com o carro em movimento**, fora do menu —
    /// que só abre parado (RF05.1). É quando mais se precisa dele, porque
    /// anoitecer acontece dirigindo. Parado, o brilho é ajustado dentro do
    /// menu, que o mostra na própria tela e dispensa este overlay.
    std::uint32_t brilho_mexido_em_ms = 0;
    bool          houve_ajuste_brilho = false;
    std::uint8_t  brilho_pct = 100;
};

/// Desenha o layout do §4.1.
///
/// **Quatro canais que não se duplicam**, e é isso que permite quatro
/// estados de via, quatro tipos de ponto e três estados degradados sem
/// ambiguidade: o número diz a que velocidade se vai, o denominador diz se
/// há limite a comparar, o ícone diz que tipo de ponto vem, e a barra diz
/// quão perto e quão grave.
///
/// **Nada aqui pisca.** A tela é o canal estável e o LED é o pulsante;
/// trocar o layout no instante de maior estresse obrigaria o motorista a
/// reaprender a tela com 9 segundos de aviso a 120 km/h.
///
/// Só redesenha o que mudou. Não é otimização prematura: a 4 Hz, redesenhar
/// a tela inteira custaria 38,4 ms de SPI por volta contra 3,4 ms da barra.
class TelaPrincipal {
public:
    /// Desenha o que mudou desde a última chamada. Devolve quantas regiões
    /// foram tocadas — zero quando nada mudou.
    int desenha(const EstadoTela& estado, std::uint32_t agora_ms,
                Visor& visor);

    /// Força o próximo `desenha` a redesenhar tudo. Para o boot e para
    /// qualquer momento em que o painel possa ter perdido o conteúdo.
    void invalida();

private:
    struct Instantaneo {
        char   numero[16] = {};
        /// O `/limite`, separado do número desde o R-64: eles vão em fontes
        /// diferentes, então não podem ser uma string só.
        char   limite[8] = {};
        char   superior[32] = {};
        char   inferior[48] = {};
        Icone  icone = Icone::Nenhum;
        int    barra_pct = -1;
        /// Deslocamento das faixas que rolam. Faz parte do instantaneo
        /// porque muda com o TEMPO, e nao com o conteudo.
        int    x_superior = 0;
        int    x_inferior = 0;
        Cor565 barra_cor = 0;
        Cor565 numero_cor = 0;
        bool   valido = false;
    };

    Instantaneo compoe(const EstadoTela& estado, std::uint32_t agora_ms) const;

    Instantaneo anterior_;
};

// ---------------------------------------------------- peças testáveis

/// Formata o relógio do §4.1 a partir da telemetria, em UTC−3.
///
/// **Não há RTC com bateria no BOM**, então antes do primeiro fix isto é
/// `--/--/-- --:--` — até 26 s em cold start. O relógio vem do GPS ou não
/// vem.
void formata_relogio(const Telemetria& t, char* destino, std::size_t tamanho);

/// `0:14` — o tempo decorrido sem sinal. Não é enfeite: `0:14` é um viaduto
/// e `3:20` é problema real, e a ação do motorista difere nos dois casos.
void formata_decorrido(std::uint32_t ms, char* destino, std::size_t tamanho);

/// Ícone do tipo de ponto, conforme a tabela do §4.1.
Icone icone_de(TipoPonto tipo);

/// Cor da barra para a zona. Verde não aparece: Zona Segura não tem barra, e
/// barra só existe perto de ponto — as cores necessárias são três.
Cor565 cor_da_barra(Zona zona);

/// Preenchimento da barra, de 0 a 100, pela distância ao alvo.
int preenchimento(float distancia_m);

}  // namespace coruja
