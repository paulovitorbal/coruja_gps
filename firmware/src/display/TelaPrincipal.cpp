#include "display/TelaPrincipal.h"

#include <cstdio>
#include <cstring>

namespace coruja {
namespace {

/// Quantos dias tem o mês, para o relógio poder retroceder o dia ao aplicar
/// UTC−3 logo depois da meia-noite.
int dias_no_mes(int mes, int ano) {
    static const int kDias[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mes < 1 || mes > 12) { return 31; }
    if (mes == 2 && ((ano % 4 == 0 && ano % 100 != 0) || ano % 400 == 0)) {
        return 29;
    }
    return kDias[mes - 1];
}

void copia(char* destino, std::size_t tamanho, const char* origem) {
    std::snprintf(destino, tamanho, "%s", origem);
}

}  // namespace

void formata_relogio(const Telemetria& t, char* destino, std::size_t tamanho) {
    if (!t.data_valida) {
        copia(destino, tamanho, "--/--/-- --:--");
        return;
    }
    int hora = static_cast<int>(t.hora) + kFusoHoras;
    int dia = static_cast<int>(t.dia);
    int mes = static_cast<int>(t.mes);
    int ano = static_cast<int>(t.ano);
    if (hora < 0) {
        // UTC−3 logo após a meia-noite volta um dia — e, no dia 1, um mês.
        hora += 24;
        if (--dia < 1) {
            if (--mes < 1) { mes = 12; --ano; }
            dia = dias_no_mes(mes, ano);
        }
    }
    std::snprintf(destino, tamanho, "%02d/%02d/%02d %02d:%02d", dia, mes,
                  ano % 100, hora, static_cast<int>(t.minuto));
}

void formata_decorrido(std::uint32_t ms, char* destino, std::size_t tamanho) {
    const unsigned s = ms / 1000U;
    std::snprintf(destino, tamanho, "%u:%02u", s / 60U, s % 60U);
}

Icone icone_de(TipoPonto tipo) {
    switch (tipo) {
        // Fixo e móvel dão o mesmo ícone de propósito: ver a nota em
        // `Icone`. O tipo continua distinto no dado e na base — o que se
        // decidiu é não gastar um canal visual com ele.
        case TipoPonto::RadarFixo:
        case TipoPonto::RadarMovel:       return Icone::Radar;
        case TipoPonto::SemaforoComRadar: return Icone::SemaforoComRadar;
        case TipoPonto::SemaforoCamera:   return Icone::Semaforo;
    }
    return Icone::Nenhum;
}

Cor565 cor_da_barra(Zona zona) {
    switch (zona) {
        case Zona::Perigo:            return paleta::kBarraPerigo;
        case Zona::AproximacaoMargem: return paleta::kBarraRosa;
        case Zona::AproximacaoConforme:
        case Zona::Semaforo:          return paleta::kBarraAmbar;
        default:                      return paleta::kMoldura;
    }
}

int preenchimento(float distancia_m) {
    if (distancia_m <= 0.0F) { return 100; }
    if (distancia_m >= kRaioAlertaM) { return 0; }
    const float fracao = 1.0F - (distancia_m / kRaioAlertaM);
    return static_cast<int>(fracao * 100.0F + 0.5F);
}

TelaPrincipal::Instantaneo TelaPrincipal::compoe(const EstadoTela& e,
                                                 std::uint32_t agora_ms) const {
    Instantaneo i;
    i.valido = true;

    // ---- número e denominador ------------------------------------------
    //
    // Fora do raio de um ponto o aparelho **não conhece o limite da via**: a
    // base é um conjunto de pontos, não uma malha viária, e `limite` é o
    // limite daquele radar. O denominador só existe perto de um ponto que
    // afira velocidade.
    if (!e.tem_fix) {
        copia(i.numero, sizeof i.numero, "- -");
        i.numero_cor = paleta::kDegradado;
    } else {
        const int v = static_cast<int>(e.telemetria.velocidade_kmh + 0.5F);
        std::snprintf(i.numero, sizeof i.numero, "%d", v);
        // O denominador é string própria porque vai em fonte menor (R-64):
        // "120/120" em 56 px daria 392 px numa tela de 320. A hierarquia
        // também concorda com a atenção — a velocidade é a informação, o
        // limite é referência.
        if (e.veredito.tem_alvo && e.veredito.alvo.limite != kSemLimite) {
            std::snprintf(i.limite, sizeof i.limite, "/%u",
                          static_cast<unsigned>(e.veredito.alvo.limite));
        }
        i.numero_cor = paleta::kTexto;
    }

    // ---- faixa superior, um ocupante por vez ----------------------------
    if (e.taxa == EstadoTaxa::Degradado || e.taxa == EstadoTaxa::Falha) {
        copia(i.superior, sizeof i.superior, "TAXA DE GPS REDUZIDA");
    } else if (e.houve_ajuste_brilho &&
               (agora_ms - e.brilho_mexido_em_ms) < kBarraBrilhoMs) {
        std::snprintf(i.superior, sizeof i.superior, "BRILHO %u%%",
                      static_cast<unsigned>(e.brilho_pct));
    } else {
        formata_relogio(e.telemetria, i.superior, sizeof i.superior);
    }

    // ---- faixa inferior, um ocupante por vez ----------------------------
    //
    // A ordem é a do §4.1, e importa: o aviso de OTA é transitório e vence
    // tudo por 2 s porque é resposta a uma ação que o motorista acabou de
    // fazer; sem ela, o clique pareceria não ter efeito.
    if (e.houve_aviso_ota && (agora_ms - e.aviso_ota_em_ms) < kAvisoOtaMs) {
        copia(i.inferior, sizeof i.inferior, "PARE O VEICULO PARA ATUALIZAR");
    } else if (!e.base_disponivel) {
        copia(i.inferior, sizeof i.inferior, "BASE INDISPONIVEL - sem alertas");
    } else if (!e.tem_fix) {
        char decorrido[16];
        formata_decorrido(agora_ms - e.sem_sinal_desde_ms, decorrido,
                          sizeof decorrido);
        std::snprintf(i.inferior, sizeof i.inferior,
                      "SEM SINAL - alertas suspensos   %s", decorrido);
    } else if (e.veredito.tem_alvo) {
        // Ícone e barra. A presença deles **é** o aviso de ponto à frente: o
        // motorista percebe que algo apareceu na faixa antes de ler dígito.
        i.icone = icone_de(e.veredito.alvo.tipo);
        i.barra_pct = preenchimento(e.veredito.distancia_m);
        i.barra_cor = cor_da_barra(e.veredito.zona);
    }
    // Em Zona Segura a faixa fica vazia de propósito: o vazio é a mensagem,
    // e é ele que dá contraste ao alerta.
    return i;
}

void TelaPrincipal::invalida() { anterior_ = Instantaneo{}; }

int TelaPrincipal::desenha(const EstadoTela& estado, std::uint32_t agora_ms,
                           Visor& visor) {
    const Instantaneo agora = compoe(estado, agora_ms);
    const bool tudo = !anterior_.valido;
    int regioes = 0;

    if (tudo) {
        // **Sem moldura.** Os dois fios de 2 px não carregavam informação e
        // gastavam área acesa, que é o que o §4.1 manda economizar: com o
        // brilho no piso, o ofuscamento vem da área, não da cor.
        visor.retangulo(0, 0, tela::kLargura, tela::kAltura, paleta::kFundo);
        ++regioes;
    }

    if (tudo || std::strcmp(agora.superior, anterior_.superior) != 0) {
        visor.retangulo(0, tela::kYFaixaSuperior, tela::kLargura,
                        tela::kFaixaSuperior, paleta::kFundo);
        // A faixa superior tem um ocupante por vez e nada à sua volta:
        // centralizado ela fica equilibrada com o número, que também é
        // centralizado, e o olho não precisa procurar onde o texto começa.
        visor.texto(tela::kLargura / 2, tela::kYFaixaSuperior + 3,
                    agora.superior, Fonte::Texto, paleta::kTexto,
                    Alinhamento::Centro);
        ++regioes;
    }

    if (tudo || std::strcmp(agora.numero, anterior_.numero) != 0 ||
        std::strcmp(agora.limite, anterior_.limite) != 0 ||
        agora.numero_cor != anterior_.numero_cor) {
        visor.retangulo(0, tela::kYAreaNumero, tela::kLargura,
                        tela::kAreaNumero, paleta::kFundo);

        // Velocidade e limite são centralizados **como conjunto**: medir só
        // o número deixaria o par deslocado para a esquerda quando houvesse
        // limite, e o conjunto dançaria ao entrar e sair de alerta.
        const int lv = largura_da_fonte(Fonte::Numero, agora.numero);
        const int ll = largura_da_fonte(Fonte::NumeroPequeno, agora.limite);
        const int x0 = (tela::kLargura - lv - ll) / 2;
        const int yv = tela::kYAreaNumero +
                       (tela::kAreaNumero -
                        altura_da_fonte(Fonte::Numero)) / 2;
        visor.texto(x0, yv, agora.numero, Fonte::Numero, agora.numero_cor,
                    Alinhamento::Esquerda);
        if (agora.limite[0] != '\0') {
            // Assenta na MESMA linha de base da velocidade. Centralizado na
            // própria altura, o limite pareceria flutuar acima do número.
            const int yl = yv + altura_da_fonte(Fonte::Numero) -
                           altura_da_fonte(Fonte::NumeroPequeno);
            visor.texto(x0 + lv, yl, agora.limite, Fonte::NumeroPequeno,
                        agora.numero_cor, Alinhamento::Esquerda);
        }
        ++regioes;
    }

    const bool inferior_mudou =
        std::strcmp(agora.inferior, anterior_.inferior) != 0 ||
        agora.icone != anterior_.icone ||
        agora.barra_pct != anterior_.barra_pct ||
        agora.barra_cor != anterior_.barra_cor;
    if (tudo || inferior_mudou) {
        visor.retangulo(0, tela::kYFaixaInferior, tela::kLargura,
                        tela::kFaixaInferior, paleta::kFundo);
        if (agora.inferior[0] != '\0') {
            // A faixa inferior fica à esquerda: o contador de tempo sem
            // sinal cresce ao fim da linha, e centralizado ele arrastaria a
            // frase inteira de lado a cada segundo.
            visor.texto(8, tela::kYFaixaInferior + 12, agora.inferior,
                        Fonte::Texto, paleta::kDegradado,
                        Alinhamento::Esquerda);
        } else if (agora.icone != Icone::Nenhum) {
            visor.icone(8, tela::kYFaixaInferior + 2, agora.icone);
            // Trilho inteiro e depois o preenchido: o trilho mostra o quanto
            // falta, que é informação, e não moldura.
            constexpr int kBarraX = 56;
            constexpr int kBarraL = tela::kLargura - kBarraX - 8;
            constexpr int kBarraA = 28;
            const int y = tela::kYFaixaInferior + 8;
            visor.retangulo(kBarraX, y, kBarraL, kBarraA, paleta::kMoldura);
            const int cheio = kBarraL * agora.barra_pct / 100;
            if (cheio > 0) {
                visor.retangulo(kBarraX, y, cheio, kBarraA, agora.barra_cor);
            }
        }
        ++regioes;
    }

    if (regioes > 0) { visor.apresenta(); }
    anterior_ = agora;
    return regioes;
}

}  // namespace coruja
