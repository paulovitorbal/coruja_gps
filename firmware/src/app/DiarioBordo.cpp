#include "app/DiarioBordo.h"

#include <cstring>

#include "log/Logger.h"
#include "nucleo/FormatoLog.h"

namespace coruja {

namespace {
constexpr const char* kOrigem = "diario";
}

bool DiarioBordo::arquivo_existe(const char* nome) {
    char sonda[2] = {};
    std::size_t lidos = 0;
    const auto r = cartao_.le_arquivo(nome, sonda, sizeof sonda, &lidos, log_);
    // `ArquivoGrande` é o caso comum de arquivo existente: a sonda tem dois
    // bytes de propósito, e qualquer log real é maior que isso.
    return r == ErroCartao::Nenhum || r == ErroCartao::ArquivoGrande;
}

void DiarioBordo::grava_infracao(const RegistroInfracao& r) {
    if (!sondou_infracoes_) {
        sondou_infracoes_ = true;
        if (!arquivo_existe(kArquivoInfracoes)) {
            cartao_.acrescenta_arquivo(kArquivoInfracoes, kCabecalhoInfracoes,
                                       std::strlen(kCabecalhoInfracoes), log_);
        }
    }

    const auto n = formata_infracao(r, trabalho_, sizeof trabalho_);
    if (n == 0) {
        log_.error(kOrigem, "linha de infracao nao coube");
        return;
    }
    if (cartao_.acrescenta_arquivo(kArquivoInfracoes, trabalho_, n, log_) !=
        ErroCartao::Nenhum) {
        log_.error(kOrigem, "falha ao gravar infracao");
        return;
    }
    log_.warning(kOrigem, trabalho_);
}

void DiarioBordo::salva_estado(bool ativa, const PontoViagem& p) {
    EstadoViagemSalvo e;
    e.ativa = ativa;
    e.ano = p.ano; e.mes = p.mes; e.dia = p.dia;
    e.hora = p.hora; e.minuto = p.minuto;
    e.dist_km = viagem_.dist_km();
    char buf[kTamEstadoViagem];
    const auto n = formata_estado(e, buf, sizeof buf);
    if (n > 0) {
        cartao_.grava_arquivo(kArquivoEstadoViagem, buf, n, log_);
    }
}

void DiarioBordo::limpa_estado() {
    EstadoViagemSalvo e;  // ativa = false
    char buf[kTamEstadoViagem];
    const auto n = formata_estado(e, buf, sizeof buf);
    if (n > 0) {
        cartao_.grava_arquivo(kArquivoEstadoViagem, buf, n, log_);
    }
}

void DiarioBordo::tenta_retomar(const Telemetria& t) {
    tentou_retomar_ = true;
    char buf[kTamEstadoViagem] = {};
    std::size_t lidos = 0;
    if (cartao_.le_arquivo(kArquivoEstadoViagem, buf, sizeof buf, &lidos,
                           log_) != ErroCartao::Nenhum) {
        return;
    }
    EstadoViagemSalvo salvo;
    if (!analisa_estado(buf, lidos, &salvo)) {
        return;
    }
    if (!pode_retomar(salvo, t)) {
        // Fora da janela: a viagem anterior acabou. Apaga a bandeira para
        // que a próxima energização não tente de novo.
        if (salvo.ativa) { limpa_estado(); }
        return;
    }
    viagem_.inicia(salvo.dist_km);
    log_.info(kOrigem, "viagem retomada");
}

/// Traduz o veredito do alerta para o que a linha de viagem registra.
///
/// Dois radares, nao um: o `alvo` venceu por GRAVIDADE (RF03.4) e e o que a
/// tela mostrou; o `mais_proximo` e o fisicamente mais perto. No Eixao os dois
/// divergem -- pista principal e lateral correm lado a lado com limites
/// diferentes --, e e essa divergencia que se quer medir.
static RadarDaAmostra radar_de(const Veredito& v) {
    RadarDaAmostra r;
    r.tem_alerta = v.tem_alvo;
    if (v.tem_alvo) {
        r.dist_alerta_m = v.distancia_m;
        r.limite_alerta = v.alvo.limite;
    }
    r.zona_pior = static_cast<std::uint8_t>(v.zona);
    r.n_candidatos = v.n_candidatos;
    r.tem_proximo = v.tem_mais_proximo;
    if (v.tem_mais_proximo) {
        r.dist_proximo_m = v.dist_mais_proximo_m;
        r.limite_proximo = v.mais_proximo.limite;
    }
    return r;
}

void DiarioBordo::trata_viagem(const Veredito& v, const Telemetria& t,
                               bool tem_fix, std::uint32_t agora_ms) {
    if (!tentou_retomar_ && tem_fix && t.data_valida && !viagem_.ativa()) {
        tenta_retomar(t);
    }

    switch (viagem_.alimenta(t, radar_de(v), tem_fix, agora_ms)) {
        case EventoViagem::Abre:
            cartao_.acrescenta_arquivo(viagem_.nome_arquivo(), kCabecalhoViagem,
                                       std::strlen(kCabecalhoViagem), log_);
            log_.info(kOrigem, viagem_.nome_arquivo());
            break;
        case EventoViagem::Grava: {
            const auto& p = viagem_.ultimo_ponto();
            const auto n = formata_ponto_viagem(p, trabalho_, sizeof trabalho_);
            if (n == 0) {
                log_.error(kOrigem, "ponto de viagem nao coube");
                break;
            }
            if (cartao_.acrescenta_arquivo(viagem_.nome_arquivo(), trabalho_, n,
                                           log_) != ErroCartao::Nenhum) {
                log_.error(kOrigem, "falha ao gravar ponto de viagem");
                break;
            }
            salva_estado(true, p);
            break;
        }
        case EventoViagem::Encerra:
            limpa_estado();
            log_.info(kOrigem, "viagem encerrada por inatividade");
            break;
        case EventoViagem::Nada:
            break;
    }
}

void DiarioBordo::passo(const Veredito& v, const Telemetria& t, bool tem_fix,
                        std::uint32_t agora_ms) {
    if (tem_fix) {
        RegistroInfracao r;
        if (det_.alimenta(v, t, &r)) {
            grava_infracao(r);
        }
    }
    trata_viagem(v, t, tem_fix, agora_ms);
}

void DiarioBordo::alterna_viagem() {
    if (viagem_.ativa()) {
        viagem_.para();
        limpa_estado();
    } else {
        // Começa do zero: a retomada com distância anterior é automática e
        // só acontece na energização, nunca por clique.
        //
        // Não marca `tentou_retomar_`: seria redundante, e a mutação provou.
        // `inicia()` deixa a viagem em `Aguardando`, que já conta como ativa,
        // e a guarda de `!viagem_.ativa()` barra a retomada sozinha.
        viagem_.inicia(0.0F);
    }
}

}  // namespace coruja
