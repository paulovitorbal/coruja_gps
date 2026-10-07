#pragma once
#include <cstddef>

#include "log/Logger.h"

namespace coruja {

class Armazenamento;

/// Escreve o log no cartão, **além** de repassá-lo ao logger seguinte.
///
/// Encadeia em vez de substituir: perder o console para ganhar o cartão seria
/// troca ruim, e o console é o que se lê enquanto se trabalha na bancada.
///
/// ## Três decisões que não são óbvias
///
/// **Acumula em RAM e grava em blocos.** Abrir, escrever e fechar um arquivo
/// a cada mensagem gastaria escrita de cartão — que tem ciclos finitos — e
/// deixaria o log mais lento que o que ele observa.
///
/// **Mas `Warning` e `Error` vão para o cartão na hora.** O acúmulo perde o
/// rabo do log num travamento, e o rabo é exatamente o que interessa quando
/// algo trava. Então o que é grave não espera o bloco encher.
///
/// **Reentrância é o risco real.** Gravar no cartão usa o `CartaoSd`, que
/// registra as próprias falhas — e se ele registrar durante uma gravação, a
/// gravação chama o log que chama a gravação. O guarda abaixo corta isso, e
/// as operações de cartão feitas por aqui recebem o logger **seguinte**, não
/// este.
class LoggerCartao final : public Logger {
public:
    /// `seguinte` recebe tudo, sempre. `cartao` só é tocado quando há o que
    /// gravar. O arquivo é aberto em modo de acréscimo, então o log sobrevive
    /// a reinicializações.
    LoggerCartao(Logger& seguinte, Armazenamento& cartao, const char* nome_arquivo);

    void registra(Nivel nivel, const char* origem,
                  const char* mensagem) override;
    void define_nivel_minimo(Nivel nivel) override;

    /// Liga ou desliga a gravação. Desligado, esta classe é um repasse puro.
    void grava_no_cartao(bool ligado);

    /// Força a ida do que está acumulado. Chame antes de algo demorado ou
    /// arriscado — o OTA, por exemplo.
    void descarrega();

    /// Desvia a gravação para outro arquivo do cartão.
    ///
    /// Existe para a **remessa**. Ela sobe o `coruja.log` e apaga o que subiu
    /// íntegro; uma linha gravada durante o próprio envio o faria crescer, e
    /// arquivo que cresceu não é apagado — ele subiria para sempre e nunca
    /// sairia do cartão. Desligar o log durante a remessa resolvia isso e
    /// criava outro problema: o envio ficava **cego**, sem uma linha no cartão
    /// nem no servidor, e três testes em campo não produziram diagnóstico
    /// nenhum (07/10/2026).
    ///
    /// Desviar resolve os dois: o `coruja.log` fica intocado e o que acontece
    /// na remessa vai para um arquivo que o `nome_enviavel` não seleciona.
    ///
    /// Descarrega o que estiver pendente antes de trocar — senão as linhas do
    /// arquivo anterior sairiam no novo.
    void usa_arquivo(const char* nome);

private:
    Logger&     seguinte_;
    Armazenamento& cartao_;
    /// Cópia, e não ponteiro: o `usa_arquivo` recebe literais de
    /// escopos que podem não sobreviver à troca.
    char        nome_[32] = {};
    Nivel       minimo_ = Nivel::Info;
    bool        ligado_ = false;
    bool        gravando_ = false;   ///< guarda de reentrância

    /// 4 KiB. Dimensionado pela **janela em que não dá para gravar**: durante
    /// o download da base a escrita de log é pulada, e essa janela gerou 14
    /// linhas numa execução real. 4 KiB cobrem ~68 linhas, com folga de quatro
    /// vezes. Com 1 KiB o log perdia o trecho inteiro.
    static constexpr std::size_t kTamBuffer = 4096;
    char        buffer_[kTamBuffer] = {};
    std::size_t usados_ = 0;
    /// Linhas descartadas por buffer cheio. Vão para o arquivo como aviso
    /// assim que a gravação voltar: buraco declarado é diagnóstico, buraco
    /// silencioso é armadilha.
    std::size_t perdidas_ = 0;
};

}  // namespace coruja
