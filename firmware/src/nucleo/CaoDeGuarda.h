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
/// **Quem decide é o maior trecho SEM REFEIÇÃO de uma operação longa**, e a
/// conta de cada operação está na tabela "O maior tempo sem refeição, por
/// operação" da seção do cão no `firmware/README.md`. Quem mexer numa espera
/// ou num prazo de rede refaz aquela conta.
///
/// ## A regra de alimentar, e o que ela proíbe de verdade
///
/// O cão existe para pegar um travamento **dentro de uma chamada do SDK**
/// (`cyw43_arch_poll`, mbedTLS, lwIP, o driver do cartão) — situação em que o
/// nosso código nem chega a testar os próprios prazos. Por isso a refeição é
/// honesta em dois lugares, e só neles:
///
///  1. **Depois de uma chamada capaz de travar ter voltado.** Se ela trava, a
///     refeição não acontece e o cão morde. É o caso de cada volta dos laços
///     de espera do `ClienteTls` e do DNS (`espera_vigiada`): alimenta-se
///     DEPOIS de `cyw43_arch_poll()`.
///  2. **Num laço que tem prazo próprio e limitado.** Se o laço gira por
///     lógica — nada chega, nada anda —, o prazo dele o encerra: os 15 s de
///     inatividade e o teto do `PrazoDeProgresso`, os 5 s do NTP, o teto de
///     60 s da tela de falha. O cão não precisa cobrir a estagnação lógica,
///     que o código já cobre, e limitada.
///
/// O que fica proibido é alimentar **por tempo decorrido**, ou num laço que
/// não passa por nenhuma chamada capaz de travar, ou que não tem limite
/// próprio. Esse laço manteria o cão saciado para sempre sobre um aparelho
/// mudo.
///
/// ## Por que a versão anterior estava errada
///
/// Este comentário afirmava que "15 s é a maior espera legítima" e que os
/// 1,5 s restantes bastavam. **É falso:** 15 s é a maior espera de CADA FASE
/// (DNS, conexão e handshake, resposta), e as fases se somam. Num Wi-Fi a
/// -88 dBm — DNS 2 s, handshake 9 s, resposta 6 s — são 17 s, mais que os
/// 16,5 s, sem nenhum travamento: o cão mordia um aparelho vivo. Hoje o
/// `ClienteTls` alimenta a cada volta, e o prazo precisa cobrir só o trecho
/// entre duas refeições, que é uma volta do laço (ver a tabela do README).
///
/// Mantém-se 16,5 s: acima de qualquer chamada única de cartão ou de rede que
/// seja legítima, e abaixo do teto do hardware.
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
