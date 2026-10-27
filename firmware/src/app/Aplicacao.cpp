#include "app/Aplicacao.h"

#include <cstdio>

#include "log/Logger.h"

namespace coruja {

namespace {
constexpr const char* kOrigem = "app";
/// Origem própria para o que descreve o receptor, e não a aplicação: quem
/// procura um problema de GPS no log filtra por isto.
constexpr const char* kOrigemGps = "gps";

/// Quanto a taxa baixa precisa durar **com fix** para virar linha de log.
///
/// Mais que `kFixVelhoMs`, de proposito: e o tempo em que a perda de sinal
/// ainda se parece com degradacao, porque o ultimo fix continua recente
/// enquanto a janela ja caiu.
constexpr std::uint32_t kSustentacaoAvisoTaxaMs = 3000;

/// Quanto a taxa precisa ficar nominal para o episódio ser dado por encerrado.
///
/// 🔴 **Mais longo que a sustentação da queda, de propósito.** Até
/// 27/10/2026 a volta não exigia sustentação nenhuma: bastava uma janela
/// nominal para sair "normalizada", e a próxima queda abria episódio novo.
/// Com a taxa oscilando em volta do limiar isso produziu 463 linhas no
/// histórico de 108 partidas — `degradado`/`normalizada` alternando a cada
/// poucos segundos, duas escritas de cartão por balanço, sem dizer nada que
/// a primeira linha já não tivesse dito.
///
/// Afirmar que normalizou é afirmação tão forte quanto afirmar que caiu, e
/// pede ao menos a mesma evidência.
constexpr std::uint32_t kSustentacaoNormalizacaoMs = 10000;
}

Aplicacao::Aplicacao(LeitorGps& gps, Encoder& encoder, PilotoAlerta& piloto,
                     Brilho& brilho, Retroiluminacao& luz, LedRegulado& led,
                     Armazenamento& cartao, AcoesAplicacao& acoes, Logger& log,
                     const Configuracao& inicial, char* trabalho,
                     std::size_t capacidade, Visor* visor)
    : gps_(gps), encoder_(encoder), piloto_(piloto), brilho_(brilho),
      luz_(luz), led_(led),
      cartao_(cartao), acoes_(acoes), log_(log), diario_(cartao, log),
      menu_(inicial),
      visor_(visor), trabalho_(trabalho), capacidade_(capacidade) {
    // O que veio do cartao vale desde a primeira volta, antes de qualquer
    // evento: um aparelho que so obedece ao arquivo depois que alguem mexe
    // no encoder nao lembra de nada.
    aplica_ajustes();
}

void Aplicacao::aplica_brilho() {
    // So escreve quando MUDA. O laco corre centenas de vezes por segundo, e
    // reescrever o registrador do PWM a cada volta seria trabalho por nada
    // num valor que muda quando alguem gira o encoder.
    const std::uint16_t duty = brilho_.duty();
    if (duty == duty_aplicado_) { return; }
    duty_aplicado_ = duty;
    luz_.define_duty(duty);
}

void Aplicacao::aplica_ajustes() {
    const Configuracao& c = menu_.ajustes();
    // O menu e a unica fonte de brilho desde que existe; aplicar todo
    // ciclo e o que faz a tela seguir o giro antes de gravar.
    brilho_.define_presets(c.brilho_dia, c.brilho_noite);
    piloto_.define_modo_noturno(c.modo_noturno);
    // O interruptor do LED segue a configuracao a cada volta, como o resto.
    // `liga()` e idempotente: so toca no hardware quando o valor muda.
    led_.liga(c.led_ligado);
    led_.define_presets(c.led_dia, c.led_noite);
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
        case AcaoMenu::EnviarDados:   acoes_.envia_dados(); break;
        case AcaoMenu::TestarAlertas: acoes_.testa_alertas(); break;
        case AcaoMenu::AlternarViagem: diario_.alterna_viagem(); break;
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

    // **A taxa congela na ENTRADA da tela de informação.** A primeira versão
    // a atualizava a cada volta, com a justificativa de que mostrar a de
    // quando o menu abriu "seria mostrar o passado". Estava errado: esta é a
    // única tela do aparelho onde se LÊ em vez de relancear, e número
    // tremendo enquanto se lê é ruído, não informação. A taxa instantânea já
    // tem lugar próprio na tela de dirigir.
    const bool informando =
        aberto && menu_.estado() == EstadoMenu::Informando;
    if (informando && !informando_antes_) {
        info_.taxa_hz = gps_.monitor().taxa_hz();
        // Congelados pelo mesmo motivo da taxa: esta é a tela onde se LÊ, e
        // número tremendo enquanto se lê é ruído.
        info_.satelites = gps_.tem_qualidade(agora_ms)
                              ? gps_.qualidade().satelites : 0;
        info_.hdop = gps_.tem_qualidade(agora_ms)
                         ? gps_.qualidade().hdop : 0.0F;
    }
    informando_antes_ = informando;

    if (aberto) {
        std::snprintf(info_.nome, sizeof info_.nome, "%s",
                      menu_.ajustes().nome);
        tela_menu_.desenha(menu_, info_, agora_ms, *visor_);
        return;
    }

    EstadoTela e;
    e.veredito = piloto_.veredito();
    e.telemetria = gps_.telemetria();
    e.tem_fix = gps_.tem_fix(agora_ms);
    // ⚠️ `tem_qualidade` ANTES de ler o HDOP. RMC e GGA chegam separadas e
    // envelhecem separadas; sem a bandeira, um HDOP de minutos atrás seria
    // desenhado ao lado de uma posição recente, afirmando uma precisão que
    // já não existe.
    e.hdop = gps_.tem_qualidade(agora_ms)
                 ? classifica_hdop(gps_.qualidade().hdop)
                 : FaixaHdop::Desconhecida;
    e.sem_sinal_desde_ms = sem_sinal_desde_ms_;
    e.aviso_ota_em_ms = aviso_ota_em_ms_;
    e.houve_aviso_ota = houve_aviso_ota_;
    e.brilho_pct = brilho_.percentual();
    e.brilho_mexido_em_ms = brilho_mexido_em_ms_;
    e.houve_ajuste_brilho = houve_ajuste_brilho_;
    tela_.desenha(e, agora_ms, *visor_);
}

void Aplicacao::anota_taxa(std::uint32_t agora_ms, bool tem_fix) {
    const EstadoTaxa estado = gps_.monitor().estado();
    // **Só com fix**, e isto é o que separa defeito de túnel. A taxa conta
    // fixes aproveitados: sem posição ela é zero por definição — num túnel,
    // numa garagem, no cold start —, e avisar ali encheria o log de linhas
    // que não descrevem defeito nenhum. A queda que interessa é a que
    // acontece COM o receptor entregando posição, porque aí a posição
    // envelhece entre uma amostra e a seguinte sem ninguém perceber.
    const bool baixa = tem_fix && (estado == EstadoTaxa::Degradado ||
                                   estado == EstadoTaxa::Falha);
    if (!baixa) {
        caindo_ = false;
        if (!taxa_avisada_) {
            pior_estado_ = EstadoTaxa::Nominal;
            return;
        }
        // Sem fix o aviso apenas deixa de valer, e dizer "normalizou" seria
        // falso: a taxa continua em zero, agora por falta de céu.
        if (estado != EstadoTaxa::Nominal) {
            // Não é recuperação: é o túnel. Fecha o episódio calado, e o
            // relógio da volta recomeça se ela vier.
            normalizando_ = false;
            taxa_avisada_ = false;
            pior_estado_ = EstadoTaxa::Nominal;
            return;
        }
        // ⚠️ **A volta precisa DURAR**, como a queda. Ver
        // `kSustentacaoNormalizacaoMs`: sem isto um balanço em volta do
        // limiar fechava e reabria o episódio a cada poucos segundos.
        if (!normalizando_) {
            normalizando_ = true;
            normalizando_desde_ms_ = agora_ms;
            return;
        }
        if ((agora_ms - normalizando_desde_ms_) < kSustentacaoNormalizacaoMs) {
            return;
        }
        normalizando_ = false;
        taxa_avisada_ = false;
        pior_estado_ = EstadoTaxa::Nominal;
        char fim[96];
        std::snprintf(fim, sizeof fim, "taxa de fixes normalizada: %.1f Hz",
                      static_cast<double>(gps_.monitor().taxa_hz()));
        log_.info(kOrigemGps, fim);
        return;
    }
    // Voltou a cair antes de a recuperação se firmar: o episódio nunca
    // fechou, e nenhuma linha nova é devida a menos que ele tenha PIORADO.
    normalizando_ = false;

    // ⚠️ **A condição precisa DURAR**, e não é detalhe: na entrada de um
    // túnel o último fix ainda é recente por 1 s (`kFixVelhoMs`) enquanto a
    // janela de 3 s já esvaziou, e nesse vão as duas condições valem ao
    // mesmo tempo. Sem a sustentação, todo viaduto deixava uma linha de
    // aviso — foi o que `OTunelNaoVirouAvisoDeTaxaNoLog` pegou.
    if (!caindo_) {
        caindo_ = true;
        caindo_desde_ms_ = agora_ms;
    }
    // 🔴 **Avisa de novo quando PIORA**, e não só na primeira vez.
    //
    // Até 2026-10-26 esta linha saía uma vez por episódio, aos 3 s — e a
    // `Falha` só existe aos 5 s (`kSustentacaoFalhaMs`). O resultado é que o
    // diário NUNCA dizia "falha" no caso grave: em 24/10 o receptor passou
    // 487 s a 1,3 Hz e as 254 linhas do histórico dizem todas "degradado",
    // contra 15 de "falha". Não era o monitor que não escalava; era o aviso
    // que não voltava a olhar.
    const bool piorou = taxa_avisada_ && estado == EstadoTaxa::Falha &&
                        pior_estado_ != EstadoTaxa::Falha;
    if (!piorou &&
        (taxa_avisada_ ||
         (agora_ms - caindo_desde_ms_) < kSustentacaoAvisoTaxaMs)) {
        return;
    }
    taxa_avisada_ = true;
    pior_estado_ = estado;
    char msg[96];
    std::snprintf(msg, sizeof msg,
                  "taxa de fixes em %.1f Hz com posicao presente (%s)",
                  static_cast<double>(gps_.monitor().taxa_hz()),
                  descreve(estado));
    log_.warning(kOrigemGps, msg);
}

void Aplicacao::invalida_tela() {
    tela_.invalida();
    tela_menu_.invalida();
}

void Aplicacao::define_base_carregada(const CabecalhoBase& cabecalho,
                                      std::size_t pontos) {
    info_.ano = cabecalho.ano;
    info_.mes = cabecalho.mes;
    info_.dia = cabecalho.dia;
    info_.pontos = pontos;
}

void Aplicacao::define_versao(const char* v) {
    std::snprintf(info_.versao, sizeof info_.versao, "%s",
                  v != nullptr ? v : "?");
}

void Aplicacao::passo(std::uint32_t agora_ms) {
    // Primeiro o alerta, sempre. Ele nao pode esperar o menu.
    piloto_.passo(agora_ms);

    const bool tem_fix = gps_.tem_fix(agora_ms);

    // **Os dois registros em cartao vem logo depois do alerta, e antes do
    // menu.** Uma infracao consumada nao pode esperar o usuario soltar o
    // encoder, e o ponto de viagem tem hora marcada.
    diario_.passo(piloto_.veredito(), gps_.telemetria(), tem_fix, agora_ms);

    anota_taxa(agora_ms, tem_fix);

    if (tem_fix) {
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
    menu_.define_estado_viagem(diario_.estado_viagem());
    brilho_.define_periodo(piloto_.periodo());
    // ⚠️ Ao lado do brilho da tela, e nao em outro lugar: as duas coisas
    // respondem ao mesmo anoitecer, e separa-las seria deixar o LED claro
    // numa tela escura.
    led_.define_periodo(piloto_.periodo());

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

    // 🔴 **A luz tambem segue o que esta volta decidiu**, e nao o que o
    // arranque tinha decidido. Ate 19/10/2026 esta linha nao existia: girar
    // o encoder mudava o `Brilho`, a barra da faixa superior e o arquivo no
    // cartao, e o PWM do backlight so era escrito uma vez, no `main`. A tela
    // so esmaecia no religamento seguinte.
    //
    // Fica AQUI, uma vez por volta, e nao junto de cada `aumenta()`: assim
    // todo caminho que mexa no brilho -- o giro, o menu, a virada para a
    // noite, a configuracao recem-lida do cartao -- passa por ela sem
    // precisar lembrar de chamar nada. Foi esquecer uma chamada dessas que
    // criou o defeito.
    aplica_brilho();

    // Desenha por ultimo: a tela mostra o que esta volta decidiu, e nao o
    // que a anterior tinha decidido.
    desenha(agora_ms);
}

}  // namespace coruja
