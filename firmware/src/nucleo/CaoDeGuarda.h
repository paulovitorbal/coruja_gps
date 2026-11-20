#pragma once
#include <cstdint>

namespace coruja {

/// Teto do contador do watchdog do RP2350, em ms.
///
/// São 24 bits de microssegundos: 16 777 215 us. O RP2040 vale metade disso
/// — 8,3 s —, porque lá o contador decrementa DUAS vezes por tick (errata
/// RP2040-E1), e o SDK corrige isso com um fator que no RP2350 é 1. Conferido
/// em `hardware_watchdog/watchdog.c` (`WATCHDOG_XFACTOR`) e na documentação de
/// `watchdog_enable`, que diz 8388 ms para o RP2040 e 16777 ms para o RP2350.
///
/// ⚠️ O SDK **trunca em silêncio** o que passar daqui. Um prazo pedido acima
/// do teto viraria um prazo menor sem avisar ninguém, e é para isso que esta
/// constante existe: quem escolhe o prazo confere contra ela em tempo de
/// compilação.
constexpr std::uint32_t kPrazoMaximoCaoMs = 16'777;

/// Quanto tempo o aparelho pode ficar sem dar sinal de progresso antes de o
/// cão morder.
///
/// **O laço principal não é quem decide este número.** A `vigia` mede a pior
/// volta do laço a cada cinco minutos, e o estado saudável é a dezena de
/// milissegundos — o pior caso conhecido são os ~53 ms do redesenho completo
/// da faixa do número (`formato_logs.md`). Contra isso, qualquer prazo acima
/// de um segundo já teria trezentas vezes de folga.
///
/// **Quem decide é a operação longa com a maior espera LEGÍTIMA**, e ela é o
/// `kInatividadeHttpMs` do `ClienteTls`: 15 s sem um byte andar. Esse silêncio
/// não é travamento — é o enlace fraco de 09/10/2026, e o próprio cliente
/// desiste dele e devolve o controle. Um prazo menor que 15 s reiniciaria o
/// aparelho em toda rede ruim, que é o erro caro: perde a viagem em andamento
/// para consertar algo que já tinha conserto.
///
/// Sobra pouca escolha, então: acima de 15 s e abaixo dos 16,777 s que o
/// hardware aceita. 16,5 s deixa 1,5 s para o `ClienteTls` registrar a
/// desistência e voltar — e é a prova de que o teto do RP2350 cobre este
/// firmware **por pouco**. Encompridar qualquer espera de rede acima de 15 s
/// passa a exigir alimentar o cão por dentro dela.
constexpr std::uint32_t kPrazoCaoMs = 16'500;

static_assert(kPrazoCaoMs <= kPrazoMaximoCaoMs,
              "o SDK truncaria este prazo em silencio");

/// O cão de guarda: reinicia o aparelho quando ninguém mais o alimenta.
///
/// **Existe por causa do pior modo de falha deste produto.** Um travamento no
/// cartão, no cyw43 ou no mbedTLS deixa o detector mudo no carro — tela
/// parada, alerta desligado — e nada avisa. O motorista não reinicia o
/// aparelho dirigindo, e o defeito só aparece depois, como "naquele dia ele
/// não apitou".
///
/// É interface pelo mesmo motivo de `Pausa` e de `Armazenamento`: a REGRA —
/// onde alimentar e, sobretudo, onde NÃO alimentar — é o que precisa de
/// teste, e ela se testa no host contra um dublê. O registrador do RP2350
/// não.
class CaoDeGuarda {
public:
    virtual ~CaoDeGuarda() = default;

    /// Começa a vigiar, e guarda o prazo para `retoma()`.
    virtual void arma(std::uint32_t prazo_ms) = 0;

    /// Renova o prazo. Só se chama onde houve PROGRESSO observável.
    virtual void alimenta() = 0;

    /// Solta o cão até `retoma()`.
    ///
    /// ⚠️ **Abre um buraco na vigilância, e por isso tem um único uso
    /// legítimo:** uma chamada do SDK que bloqueia por mais que
    /// `kPrazoMaximoCaoMs` e não oferece gancho nenhum por dentro. Hoje é a
    /// associação Wi-Fi — `cyw43_arch_wifi_connect_timeout_ms` com 20 s, mais
    /// a varredura com 12 s. Mantê-lo armado ali seria reiniciar o aparelho
    /// em TODA conexão que falha, e falhar é desfecho comum (senha errada,
    /// rede fora de alcance), não travamento.
    virtual void suspende() = 0;

    /// Volta a vigiar com o prazo da última `arma()`.
    virtual void retoma() = 0;

    /// Esta ligação começou por uma mordida do cão?
    ///
    /// Sem isto o defeito seria invisível: o aparelho reiniciaria sozinho, o
    /// log recomeçaria com um `boot` igual a qualquer outro, e quem lesse
    /// concluiria que alguém cortou a ignição.
    virtual bool reiniciou_por_travamento() const = 0;
};

}  // namespace coruja
