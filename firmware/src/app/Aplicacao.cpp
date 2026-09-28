#include "app/Aplicacao.h"

#include "log/Logger.h"

namespace coruja {

namespace {
constexpr const char* kOrigem = "app";
}

Aplicacao::Aplicacao(LeitorGps& gps, Encoder& encoder, PilotoAlerta& piloto,
                     Brilho& brilho, Armazenamento& cartao,
                     AcoesAplicacao& acoes, Logger& log,
                     const Configuracao& inicial, char* trabalho,
                     std::size_t capacidade)
    : gps_(gps), encoder_(encoder), piloto_(piloto), brilho_(brilho),
      cartao_(cartao), acoes_(acoes), log_(log), menu_(inicial),
      trabalho_(trabalho), capacidade_(capacidade) {
    // O que veio do cartao vale desde a primeira volta, antes de qualquer
    // evento: um aparelho que so obedece ao arquivo depois que alguem mexe
    // no encoder nao lembra de nada.
    aplica_ajustes();
}

void Aplicacao::aplica_ajustes() {
    const Configuracao& c = menu_.ajustes();
    // O menu e a unica fonte de brilho desde que existe; aplicar todo
    // ciclo e o que faz a tela seguir o giro antes de gravar.
    brilho_.define_presets(c.brilho_dia, c.brilho_noite);
    piloto_.define_modo_noturno(c.modo_noturno);
}

void Aplicacao::executa(AcaoMenu acao) {
    switch (acao) {
        case AcaoMenu::Gravar: {
            ++gravacoes_;
            ultima_gravacao_ = grava_ajustes(cartao_, menu_.ajustes(),
                                             trabalho_, capacidade_, log_);
            if (ultima_gravacao_ != ResultadoGravacao::Gravado) {
                // Nao desfaz o ajuste: ele continua valendo nesta sessao.
                // Perder o cartao nao e razao para escurecer a tela de
                // volta na cara de quem acabou de ajustar.
                log_.error(kOrigem, descreve(ultima_gravacao_));
            }
            break;
        }
        case AcaoMenu::AtualizarBase: acoes_.atualiza_base(); break;
        case AcaoMenu::TestarAlertas: acoes_.testa_alertas(); break;
        case AcaoMenu::Nenhuma:       break;
    }
}

void Aplicacao::passo(std::uint32_t agora_ms) {
    // Primeiro o alerta, sempre. Ele nao pode esperar o menu.
    piloto_.passo(agora_ms);

    if (gps_.tem_fix(agora_ms)) {
        detector_.atualiza(gps_.telemetria().velocidade_kmh, agora_ms);
    } else {
        detector_.sem_fix(agora_ms);
    }

    menu_.define_periodo(piloto_.periodo());
    brilho_.define_periodo(piloto_.periodo());

    // Drena a fila do encoder. Um detente perdido por volta seria um
    // giro que nao aparece na tela, e o laco corre mais rapido que o
    // GPS justamente para isso nao acontecer. A ultima passada chega
    // com `Nenhum`, que e o que cobra o timeout do menu.
    EventoEncoder evento = EventoEncoder::Nenhum;
    do {
        evento = encoder_.proximo_evento();
        const AcaoMenu acao =
            menu_.avalia(evento, detector_.parado(), agora_ms);
        aplica_ajustes();
        executa(acao);
    } while (evento != EventoEncoder::Nenhum);
}

}  // namespace coruja
