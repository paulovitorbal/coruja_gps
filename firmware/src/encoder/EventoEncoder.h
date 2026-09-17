#pragma once

namespace coruja {

/// O que o encoder produziu desde a ultima leitura.
///
/// Um detente de giro e um clique sao eventos discretos, nao estados: quem
/// consome reage uma vez e esquece. E por isso que nao ha "botao pressionado"
/// aqui — se algum requisito precisar de pressao longa, isso vira um evento
/// proprio em vez de expor o estado cru do pino.
enum class EventoEncoder {
    Nenhum,
    GiroEsquerda,  ///< anti-horario
    GiroDireita,   ///< horario
    Clique,
};

const char* nome_evento(EventoEncoder e);

}  // namespace coruja
