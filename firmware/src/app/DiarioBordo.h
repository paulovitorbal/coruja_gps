#pragma once
#include <cstdint>

#include "armazenamento/Armazenamento.h"
#include "nucleo/AcumuladorViagem.h"
#include "nucleo/DetectorInfracao.h"
#include "nucleo/RetomadaViagem.h"
#include "nucleo/Zonamento.h"

namespace coruja {

class Logger;

constexpr const char* kArquivoInfracoes = "infracoes.log";

/// Os dois registros em cartão: infrações consumadas e trajeto de viagem.
///
/// **Junta detector, acumulador e cartão numa peça só** para que a
/// `Aplicacao` continue sendo fiação. Toda a lógica já vive nas peças
/// portáveis; o que esta classe acrescenta é *quando gravar o quê*, e isso
/// também é testável no host porque depende de `Armazenamento`, não de
/// `CartaoSd`.
///
/// ## Grava na hora, sem acumular em RAM
///
/// A alimentação é pós-chave e cai sem aviso. Um ponto por minuto e uma
/// infração por radar são escrita irrisória para o cartão — e perder o
/// último trecho de uma viagem de teste seria pior do que o desgaste que se
/// evitaria. O `LoggerCartao`, que grava muito mais, é que precisa de buffer.
///
/// ## A retomada depois do corte de energia
///
/// Cada ponto gravado atualiza `viagem.est`. Na energização seguinte, o
/// primeiro fix compara o relógio com o que está salvo: dentro de
/// `kJanelaRetomadaMin` a viagem continua, em arquivo novo e com a distância
/// de onde parou. Fora dela, nada acontece e o estado é descartado.
class DiarioBordo {
public:
    DiarioBordo(Armazenamento& cartao, Logger& log)
        : cartao_(cartao), log_(log) {}

    /// Uma volta do laço principal.
    void passo(const Veredito& v, const Telemetria& t, bool tem_fix,
               std::uint32_t agora_ms);

    /// O que o item de menu pede: inicia se parada, encerra se gravando.
    void alterna_viagem();

    EstadoViagem estado_viagem() const { return viagem_.estado(); }
    float dist_viagem_km() const { return viagem_.dist_km(); }

private:
    void grava_infracao(const RegistroInfracao& r);
    void trata_viagem(const Veredito& v, const Telemetria& t, bool tem_fix,
                      std::uint32_t agora_ms);
    void tenta_retomar(const Telemetria& t);
    void salva_estado(bool ativa, const PontoViagem& p);
    void limpa_estado();
    bool arquivo_existe(const char* nome);

    Armazenamento& cartao_;
    Logger&        log_;

    DetectorInfracao det_;
    AcumuladorViagem viagem_;

    /// A sondagem de existência do `infracoes.log` é feita uma vez por
    /// energização: o cabeçalho só vai no arquivo novo, e reler o cartão a
    /// cada infração para descobrir isso seria leitura por nada.
    bool sondou_infracoes_ = false;

    /// A retomada é tentada uma vez só por energização.
    ///
    /// **E o motivo é desgaste de cartão, não correção** — a mutação
    /// desmentiu a justificativa que eu havia escrito aqui. Contra reabrir a
    /// viagem já existem duas barreiras: `!viagem_.ativa()`, porque
    /// `Aguardando` já conta como ativa; e o próprio arquivo de estado, que é
    /// zerado ao encerrar.
    ///
    /// O que sobra é o caso de o estado **não dar para ler** — sem cartão, ou
    /// arquivo corrompido. Aí nada acima impede a tentativa, e sem esta
    /// bandeira seriam quatro leituras de cartão por segundo, para sempre,
    /// por nada.
    bool tentou_retomar_ = false;

    char trabalho_[192] = {};
};

}  // namespace coruja
