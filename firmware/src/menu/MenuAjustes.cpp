#include "menu/MenuAjustes.h"

#include <cstdio>

#include "display/Brilho.h"

namespace coruja {

namespace {

int gira(EventoEncoder e) {
    if (e == EventoEncoder::GiroDireita) { return +1; }
    if (e == EventoEncoder::GiroEsquerda) { return -1; }
    return 0;
}

std::uint8_t soma_limitada(std::uint8_t v, int passo, std::uint8_t minimo,
                           std::uint8_t maximo) {
    const int novo = static_cast<int>(v) + passo;
    if (novo < static_cast<int>(minimo)) { return minimo; }
    if (novo > static_cast<int>(maximo)) { return maximo; }
    return static_cast<std::uint8_t>(novo);
}

void copia_texto(char* destino, std::size_t capacidade, const char* texto) {
    if (capacidade == 0) { return; }
    std::snprintf(destino, capacidade, "%s", texto);
}

}  // namespace

std::uint8_t& MenuAjustes::brilho_vigente() {
    return periodo_ == PeriodoDoDia::Noite ? cfg_.brilho_noite
                                           : cfg_.brilho_dia;
}

std::uint8_t MenuAjustes::brilho_vigente() const {
    return periodo_ == PeriodoDoDia::Noite ? cfg_.brilho_noite
                                           : cfg_.brilho_dia;
}

std::uint8_t& MenuAjustes::led_vigente() {
    return periodo_ == PeriodoDoDia::Noite ? cfg_.led_noite : cfg_.led_dia;
}

std::uint8_t MenuAjustes::led_vigente() const {
    return periodo_ == PeriodoDoDia::Noite ? cfg_.led_noite : cfg_.led_dia;
}

void MenuAjustes::registra_brilho_externo(std::uint8_t pct) {
    // **Nao marca `alterado_`, de proposito.** O `alterado_` governa a
    // gravacao no FECHAMENTO do menu, e este caminho nao tem fechamento:
    // quem ajusta dirigindo decide gravar por repouso do encoder. Marcar
    // aqui seriam dois mecanismos para a mesma decisao, e o segundo nunca
    // seria exercitado -- foi o que a mutacao mostrou.
    brilho_vigente() = pct;
}

void MenuAjustes::abre(std::uint32_t agora_ms) {
    estado_ = EstadoMenu::Navegando;
    item_ = ItemMenu::Brilho;
    alterado_ = false;
    ultimo_evento_ms_ = agora_ms;
}

AcaoMenu MenuAjustes::fecha() {
    estado_ = EstadoMenu::Fechado;
    const bool gravar = alterado_;
    alterado_ = false;
    return gravar ? AcaoMenu::Gravar : AcaoMenu::Nenhuma;
}

void MenuAjustes::anda(int passo) {
    int i = static_cast<int>(item_) + passo;
    const int n = static_cast<int>(kItensMenu);
    // Circular: com sete itens, dar a volta e mais rapido que voltar
    // girando, e nao ha "fim de lista" para o motorista descobrir.
    i = (i % n + n) % n;
    item_ = static_cast<ItemMenu>(i);
}

void MenuAjustes::edita(int passo) {
    switch (item_) {
        case ItemMenu::Brilho: {
            std::uint8_t& b = brilho_vigente();
            const std::uint8_t novo =
                soma_limitada(b, passo * 5, kBrilhoMinimoPct,
                              kBrilhoMaximoPct);
            alterado_ = alterado_ || novo != b;
            b = novo;
            break;
        }
        case ItemMenu::ModoNoturno: {
            // Tres posicoes, sem circular: bater na ponta e a unica pista
            // tatil de que a lista acabou.
            int m = static_cast<int>(cfg_.modo_noturno) + passo;
            if (m < 0) { m = 0; }
            if (m > 2) { m = 2; }
            const auto novo = static_cast<ModoNoturno>(m);
            alterado_ = alterado_ || novo != cfg_.modo_noturno;
            cfg_.modo_noturno = novo;
            break;
        }
        case ItemMenu::BrilhoLed: {
            // Piso em 5, e nao em zero: zero e o que o item `led` faz, e dois
            // ajustes capazes de apagar o LED disputariam o mesmo resultado.
            std::uint8_t& l = led_vigente();
            const std::uint8_t novo = soma_limitada(l, passo * 5, 5, 100);
            alterado_ = alterado_ || novo != l;
            l = novo;
            break;
        }
        case ItemMenu::Led: {
            // Duas posicoes: qualquer giro alterna. Nao circula porque nao
            // ha por onde circular -- com dois valores, "bater na ponta"
            // seria nao fazer nada, e um giro sem efeito parece defeito.
            const bool novo = passo > 0;
            alterado_ = alterado_ || novo != cfg_.led_ligado;
            cfg_.led_ligado = novo;
            break;
        }
        default:
            break;  // itens de acao nao editam
    }
}

AcaoMenu MenuAjustes::avalia(EventoEncoder evento, bool parado,
                             std::uint32_t agora_ms) {
    // Andar fecha, aconteca o que acontecer. Vem antes de tudo porque
    // nenhuma edicao em curso justifica tapar o velocimetro em movimento.
    if (!parado) {
        return aberto() ? fecha() : AcaoMenu::Nenhuma;
    }

    if (estado_ == EstadoMenu::Fechado) {
        if (gira(evento) != 0) {
            abre(agora_ms);
        }
        // O clique com o carro parado e do OTA (RF05), nao do menu.
        return AcaoMenu::Nenhuma;
    }

    if (evento == EventoEncoder::Nenhum) {
        if (agora_ms - ultimo_evento_ms_ >= kTimeoutMenuMs) {
            return fecha();
        }
        return AcaoMenu::Nenhuma;
    }
    ultimo_evento_ms_ = agora_ms;

    if (estado_ == EstadoMenu::Informando) {
        estado_ = EstadoMenu::Navegando;
        return AcaoMenu::Nenhuma;
    }

    if (estado_ == EstadoMenu::Editando) {
        if (evento == EventoEncoder::Clique) {
            estado_ = EstadoMenu::Navegando;
            return AcaoMenu::Nenhuma;
        }
        edita(gira(evento));
        return AcaoMenu::Nenhuma;
    }

    // Navegando
    if (evento != EventoEncoder::Clique) {
        anda(gira(evento));
        return AcaoMenu::Nenhuma;
    }
    switch (item_) {
        case ItemMenu::AtualizarBase:
            // Fecha antes: o OTA suspende a leitura do GPS, e o menu nao
            // pode ficar por baixo dele esperando um evento que nao vem.
            fecha();
            return AcaoMenu::AtualizarBase;
        case ItemMenu::EnviarDados:
            // Fecha antes, pelo mesmo motivo do OTA: a remessa bloqueia por
            // dezenas de segundos e desenha a propria tela por cima.
            fecha();
            return AcaoMenu::EnviarDados;
        case ItemMenu::TestarAlertas:
            return AcaoMenu::TestarAlertas;
        case ItemMenu::Viagem:
            // Nao fecha o menu: alternar viagem e instantaneo, e manter o
            // item a vista deixa o novo estado visivel na segunda linha --
            // que e a confirmacao de que o clique pegou.
            return AcaoMenu::AlternarViagem;
        case ItemMenu::Informacao:
            estado_ = EstadoMenu::Informando;
            return AcaoMenu::Nenhuma;
        case ItemMenu::Sair:
            return fecha();
        default:
            estado_ = EstadoMenu::Editando;
            return AcaoMenu::Nenhuma;
    }
}

const char* MenuAjustes::rotulo(ItemMenu i) const {
    switch (i) {
        case ItemMenu::Brilho:
            return periodo_ == PeriodoDoDia::Noite ? "brilho (noite)"
                                                   : "brilho (dia)";
        case ItemMenu::ModoNoturno:   return "modo noturno";
        case ItemMenu::Led:           return "led";
        case ItemMenu::BrilhoLed:     return "brilho led";
        case ItemMenu::AtualizarBase: return "atualizar base";
        case ItemMenu::EnviarDados:   return "enviar dados";
        case ItemMenu::TestarAlertas: return "testar alertas";
        case ItemMenu::Viagem:        return "viagem";
        case ItemMenu::Informacao:    return "informacao";
        case ItemMenu::Sair:          return "sair";
    }
    return "?";
}

void MenuAjustes::valor(ItemMenu i, char* destino,
                        std::size_t capacidade) const {
    if (destino == nullptr || capacidade == 0) { return; }
    switch (i) {
        case ItemMenu::Brilho:
            std::snprintf(destino, capacidade, "%u%%", brilho_vigente());
            break;
        case ItemMenu::ModoNoturno:
            copia_texto(destino, capacidade, descreve(cfg_.modo_noturno));
            break;
        case ItemMenu::Led:
            copia_texto(destino, capacidade,
                        cfg_.led_ligado ? "ligado" : "desligado");
            break;
        case ItemMenu::BrilhoLed:
            std::snprintf(destino, capacidade, "%u%%",
                          static_cast<unsigned>(led_vigente()));
            break;
        case ItemMenu::Viagem:
            // "iniciar" ou "parar": a segunda linha diz o que o clique vai
            // FAZER, nao o estado em que se esta. Rotulo de acao e o que o
            // item de menu promete.
            copia_texto(destino, capacidade, descreve(viagem_));
            break;
        default:
            destino[0] = '\0';
            break;
    }
}

}  // namespace coruja
