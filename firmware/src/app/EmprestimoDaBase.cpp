#include "app/EmprestimoDaBase.h"

#include <cstdio>

#include "log/Logger.h"

namespace coruja {
namespace {

constexpr const char* kOrigem = "emprestimo";

}  // namespace

EmprestimoDaBase::EmprestimoDaBase(PilotoAlerta& piloto,
                                   RecarregadorBase& recarregador,
                                   Ponto* base,
                                   std::size_t capacidade_em_pontos,
                                   Logger& log)
    : piloto_(piloto), recarregador_(recarregador), base_(base), log_(log) {
    // Solta antes de entregar a area. Nao e questao de corrida: este
    // firmware e de uma linha de execucao so, e nada observa o estado entre
    // as duas chamadas -- uma campanha de mutacao confirmou que inverter a
    // ordem nao muda comportamento nenhum. E ordem de LEITURA: o invariante
    // "o piloto nao alerta" fica estabelecido antes de a memoria trocar de
    // dono, e quem le o construtor ve a garantia antes do risco.
    piloto_.solta_base();
    arena_.adota(base, capacidade_em_pontos * sizeof(Ponto));

    char msg[96];
    std::snprintf(msg, sizeof msg,
                  "base emprestada: %u B; sem alerta ate recarregar",
                  static_cast<unsigned>(arena_.capacidade()));
    log_.info(kOrigem, msg);
}

EmprestimoDaBase::~EmprestimoDaBase() {
    char msg[128];
    std::snprintf(msg, sizeof msg, "pico de uso: %u B de %u",
                  static_cast<unsigned>(arena_.pico()),
                  static_cast<unsigned>(arena_.capacidade()));
    log_.info(kOrigem, msg);
    if (arena_.faltas() > 0) {
        std::snprintf(msg, sizeof msg,
                      "faltou memoria %u vez(es): a area nao deu conta",
                      static_cast<unsigned>(arena_.faltas()));
        log_.warning(kOrigem, msg);
    }

    const std::size_t pontos = recarregador_.recarrega(log_);
    if (pontos == 0) {
        // Sem base utilizavel. NAO se devolve o ponteiro: ele aponta para o
        // que sobrou do handshake, e alertar com isso e pior que nao alertar
        // -- o motorista confia no que ve.
        log_.error(kOrigem,
                   "nao recarreguei a base: SEM ALERTA ate o proximo boot");
        return;
    }
    piloto_.define_base(base_, pontos);

    std::snprintf(msg, sizeof msg, "base recarregada: %u pontos",
                  static_cast<unsigned>(pontos));
    log_.info(kOrigem, msg);
}

}  // namespace coruja
