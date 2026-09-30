#include "app/Aplicacao.h"

#include <cstdio>

#include "log/Logger.h"

namespace coruja {

namespace {
constexpr const char* kOrigem = "app";
}

Aplicacao::Aplicacao(LeitorGps& gps, Encoder& encoder, PilotoAlerta& piloto,
                     Brilho& brilho, Armazenamento& cartao,
                     AcoesAplicacao& acoes, Logger& log,
                     const Configuracao& inicial, char* trabalho,
                     std::size_t capacidade, Visor* visor)
    : gps_(gps), encoder_(encoder), piloto_(piloto), brilho_(brilho),
      cartao_(cartao), acoes_(acoes), log_(log), menu_(inicial),
      visor_(visor), trabalho_(trabalho), capacidade_(capacidade) {
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

void Aplicacao::desenha(std::uint32_t agora_ms) {
    if (visor_ == nullptr) {
        return;  // bancadas e testes de logica rodam sem painel
    }

    // **A transicao entre as telas invalida a que entra.** As duas so
    // redesenham o que mudou, e o que a outra deixou no painel nao esta em
    // nenhum dos dois instantaneos -- sem invalidar, a tela nova apareceria
    // por cima de pedacos da anterior.
    const bool aberto = menu_.aberto();
    if (aberto != menu_no_ar_) {
        if (aberto) {
            tela_menu_.invalida();
        } else {
            tela_.invalida();
        }
        menu_no_ar_ = aberto;
    }

    if (aberto) {
        // A taxa vem do monitor a cada volta: ela muda sozinha, e mostrar
        // a de quando o menu abriu seria mostrar o passado.
        info_.taxa_hz = gps_.monitor().taxa_hz();
        std::snprintf(info_.nome, sizeof info_.nome, "%s",
                      menu_.ajustes().nome);
        tela_menu_.desenha(menu_, info_, agora_ms, *visor_);
        return;
    }

    EstadoTela e;
    e.veredito = piloto_.veredito();
    e.telemetria = gps_.telemetria();
    e.tem_fix = gps_.tem_fix(agora_ms);
    e.taxa = gps_.monitor().estado();
    e.sem_sinal_desde_ms = sem_sinal_desde_ms_;
    e.aviso_ota_em_ms = aviso_ota_em_ms_;
    e.houve_aviso_ota = houve_aviso_ota_;
    e.brilho_pct = brilho_.percentual();
    e.brilho_mexido_em_ms = brilho_mexido_em_ms_;
    e.houve_ajuste_brilho = houve_ajuste_brilho_;
    tela_.desenha(e, agora_ms, *visor_);
}

void Aplicacao::define_base_carregada(const char* versao,
                                      std::size_t pontos) {
    std::snprintf(info_.versao_base, sizeof info_.versao_base, "%s",
                  versao != nullptr ? versao : "");
    info_.pontos = pontos;
}

void Aplicacao::passo(std::uint32_t agora_ms) {
    // Primeiro o alerta, sempre. Ele nao pode esperar o menu.
    piloto_.passo(agora_ms);

    if (gps_.tem_fix(agora_ms)) {
        detector_.atualiza(gps_.telemetria().velocidade_kmh, agora_ms);
        houve_fix_ = true;
    } else {
        detector_.sem_fix(agora_ms);
        // O contador "0:14" mede desde a PERDA, nao desde o boot: antes do
        // primeiro fix nao ha o que contar, e mostrar o tempo ligado no
        // lugar diria outra coisa.
        if (houve_fix_) {
            houve_fix_ = false;
            sem_sinal_desde_ms_ = agora_ms;
        }
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
        const bool menu_estava_aberto = menu_.aberto();
        const AcaoMenu acao =
            menu_.avalia(evento, detector_.parado(), agora_ms);
        aplica_ajustes();
        executa(acao);

        // **Girar ajusta o brilho quando o menu nao consumiu o giro.**
        //
        // A condicao e essa, e nao "o carro esta andando": parado, o giro
        // abre o menu, e `menu_.aberto()` ja e verdadeiro aqui. Escrever
        // `!detector_.parado()` junto parecia mais explicito e nao
        // decidia nada -- a mutacao mostrou que remove-lo nao muda
        // comportamento nenhum. Em movimento o giro sobra, e e quando mais
        // se precisa dele: anoitecer acontece dirigindo.
        //
        // O valor vai para a CONFIGURACAO, nao so para o `Brilho`: o
        // `aplica_ajustes()` reaplica a configuracao a cada volta, e sem
        // isto o giro seria desfeito no ciclo seguinte.
        if (!menu_estava_aberto && !menu_.aberto()) {
            const bool girou = evento == EventoEncoder::GiroDireita ||
                               evento == EventoEncoder::GiroEsquerda;
            if (girou) {
                if (evento == EventoEncoder::GiroDireita) {
                    brilho_.aumenta();
                } else {
                    brilho_.diminui();
                }
                menu_.registra_brilho_externo(brilho_.percentual());
                brilho_mexido_em_ms_ = agora_ms;
                houve_ajuste_brilho_ = true;
                gravacao_pendente_ = true;
            }
        }

        // **O clique com o menu fechado e do OTA (RF05), nao do menu** -- e
        // por isso que o menu abre ao GIRAR. O `MenuAjustes` deixa o clique
        // passar de proposito; quem decide o que fazer com ele e aqui.
        if (!menu_estava_aberto && !menu_.aberto() &&
            evento == EventoEncoder::Clique) {
            if (detector_.parado()) {
                acoes_.atualiza_base();
            } else {
                // RF05.1: fora da condicao de seguranca o clique e
                // ignorado **e avisado**. Ignorar calado faria o clique
                // parecer sem efeito, e o motorista clicaria de novo.
                aviso_ota_em_ms_ = agora_ms;
                houve_aviso_ota_ = true;
                log_.warning(kOrigem,
                             "clique de OTA recusado: veiculo em movimento");
            }
        }
    } while (evento != EventoEncoder::Nenhum);

    // O brilho ajustado fora do menu vai ao cartao quando o giro cessa.
    // Gravar por detente seriam dezenas de escritas num meio de ciclos
    // finitos, e o valor intermediario nao interessa a ninguem.
    if (gravacao_pendente_ &&
        (agora_ms - brilho_mexido_em_ms_) >= kEsperaGravacaoBrilhoMs) {
        gravacao_pendente_ = false;
        executa(AcaoMenu::Gravar);
    }

    // Desenha por ultimo: a tela mostra o que esta volta decidiu, e nao o
    // que a anterior tinha decidido.
    desenha(agora_ms);
}

}  // namespace coruja
