#pragma once

namespace coruja {

class Logger;

/// O que precisa acontecer **depois de a rede subir** e **antes do primeiro
/// pedido**.
///
/// Hoje é um só: acertar o relógio. O certificado é julgado contra a data, e
/// o RP2350 não tem bateria no relógio — todo boot começa sem hora.
///
/// ⚠️ **Existe por causa de um travamento, e a ordem é o ponto inteiro.** A
/// primeira versão sincronizava a hora na composição, antes de chamar o
/// orquestrador. Mas quem chama `cyw43_arch_init()` é o `RedeWifi::conecta()`,
/// que só roda *dentro* dele — então o cliente NTP falava com a pilha de rede
/// e com o rádio **antes de os dois existirem**. O aparelho travava por
/// completo no clique de atualizar: tela congelada, encoder morto.
///
/// Por isso isto é uma interface chamada pelo orquestrador, e não uma chamada
/// na composição: **quem sabe que a rede subiu é quem acabou de levantá-la.**
///
/// Fica como interface, e não como o `SincronizadorHora` direto, para que o
/// OTA e a remessa não passem a conhecer relógio, NTP nem GPS — eles cuidam
/// de base e de arquivos.
class PreparoDeSessao {
public:
    virtual ~PreparoDeSessao() = default;

    /// Chamado uma vez por sessão, logo após a conexão dar certo.
    ///
    /// **Não devolve erro de propósito.** Sem hora, o TLS recusa o
    /// certificado e a falha aparece onde ela de fato acontece — com a
    /// mensagem do handshake, não com um código genérico vindo daqui.
    virtual void apos_conectar(Logger& log) = 0;
};

}  // namespace coruja
