#pragma once
#include <cstdint>

#include "nucleo/SincronizadorHora.h"

namespace coruja {

/// Quanto tempo esperar a resposta do servidor de hora.
///
/// Curto de propósito: o NTP é UDP e não retransmite sozinho; se o pacote se
/// perdeu, esperar não traz. Cinco segundos cobrem uma rede ruim, e o
/// `SincronizadorHora` já tem o GPS como alternativa logo atrás.
constexpr std::uint32_t kTempoLimiteNtpMs = 5000;

/// Consulta a hora por SNTP, sobre o UDP do lwIP.
///
/// Não implementa o protocolo: a montagem do pedido e a leitura da resposta
/// vivem em `Sntp.h`, em código portável com suíte própria. Aqui só há o
/// transporte — resolver o nome, mandar o pacote, esperar, entregar os bytes.
///
/// ⚠️ **Sem autenticação nenhuma.** É UDP em claro, e quem controla a rede
/// escolhe a hora que o aparelho vai acreditar. Como essa hora decide se um
/// certificado está no prazo, isso é um buraco real — e é o custo assumido de
/// dar prioridade ao NTP sobre o GPS. Ver `SincronizadorHora`.
class ClienteSntp final : public FonteNtp {
public:
    bool consulta(const char* servidor, std::int64_t* segundos,
                  Logger& log) override;
};

}  // namespace coruja
