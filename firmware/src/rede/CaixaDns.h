#pragma once
#include <cstdint>

namespace coruja {

/// Onde a resposta do DNS cai — para que ela **nunca** caia na pilha.
///
/// O lwIP não tem como cancelar um `dns_gethostbyname`: ele guarda a função e
/// o `arg` numa tabela e chama de volta sempre, inclusive quando desiste
/// (`dns.c`, `dns_call_found(i, NULL)` depois de `DNS_MAX_RETRIES`). Quem
/// passou o endereço de uma variável local e voltou por tempo esgotado deixou
/// o lwIP com um ponteiro para um quadro de pilha já reaproveitado — e a
/// escrita atrasada corrompe o que estiver ali.
///
/// O caminho mais curto para isso é o NTP, e não por pouco: o prazo dele é
/// 5 s e o lwIP desiste em `DNS_MAX_RETRIES (4) × DNS_TMR_INTERVAL (1000 ms)`.
/// Os dois prazos caem um em cima do outro, e o NTP roda em **toda** sessão de
/// rede, logo depois de associar ao Wi-Fi — que é justamente quando o DNS
/// demora.
///
/// A caixa vive pelo programa inteiro e o `arg` deixa de ser ponteiro: passa a
/// ser um **número de geração**. A resposta atrasada chega, não reconhece a
/// geração e vai embora sem escrever em nada.
///
/// Vive aqui, e não dentro do cliente, porque os clientes só compilam para o
/// RP2350: enterrada lá, esta seria a única guarda de memória do projeto sem
/// teste nenhum.
class CaixaDns {
public:
    /// `ultima_usada` existe para o teste, e não é adorno: a única invariante
    /// que o programa não consegue exercitar é a **volta do contador**, que
    /// acontece depois de 2³² pedidos. Sem poder começar perto da borda, a
    /// garantia de "nunca devolve zero" ficaria escrita e não verificada — e
    /// uma geração zero faria toda resposta atrasada ser aceita.
    explicit CaixaDns(std::uint32_t ultima_usada = 0)
        : ultima_(ultima_usada) {}

    /// Abre um pedido e devolve a geração que o identifica.
    ///
    /// Nunca devolve zero, e nunca repete a geração de um pedido anterior —
    /// é disso que depende reconhecer a resposta atrasada.
    std::uint32_t abre();

    /// O pedido em curso deixou de interessar (tempo esgotado).
    ///
    /// Não cancela nada no lwIP, porque não há como: só faz a resposta que
    /// ainda vier não ser reconhecida por ninguém.
    void abandona();

    /// A resposta chegou. `achou == false` é o lwIP desistindo.
    ///
    /// Devolve `true` quando a resposta é do pedido em curso — e **só então**
    /// quem chama pode copiar o endereço. O endereço não mora aqui de
    /// propósito: ele é um `ip_addr_t` do lwIP, e esta caixa precisa compilar
    /// no host para ser testada. Guardá-lo aqui também inverteria a ordem
    /// certa: copiar primeiro e perguntar depois deixaria uma resposta
    /// atrasada escrever por cima do endereço de um pedido novo já resolvido.
    bool entrega(std::uint32_t geracao, bool achou);

    bool pronto() const { return pronto_; }
    bool achou() const { return achou_; }

private:
    /// Zero quer dizer "nenhum pedido em curso", e por isso `abre` pula o
    /// zero ao dar a volta.
    std::uint32_t em_curso_ = 0;
    std::uint32_t ultima_ = 0;
    bool          pronto_ = false;
    bool          achou_ = false;
};

}  // namespace coruja
