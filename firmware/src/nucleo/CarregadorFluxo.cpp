#include "nucleo/CarregadorFluxo.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "log/Logger.h"

namespace coruja {

CarregadorFluxo::CarregadorFluxo(Ponto* destino, std::size_t capacidade)
    : destino_(destino), capacidade_(capacidade) {}

void CarregadorFluxo::reinicia() {
    verificador_.reinicia();
    escritos_ = 0;
    parcial_n_ = 0;
    lat_anterior_ = -2147483647 - 1;
    erro_de_registro_ = ErroBase::Nenhum;
}

void CarregadorFluxo::consome_registro(const std::uint8_t* registro) {
    if (escritos_ >= capacidade_) {
        // Para de escrever, mas continua consumindo: o CRC precisa de todos os
        // bytes, e parar no meio daria "CRC invalido" para um arquivo que na
        // verdade só é grande demais para esta placa.
        if (erro_de_registro_ == ErroBase::Nenhum) {
            erro_de_registro_ = ErroBase::ExcedeuCapacidade;
        }
        return;
    }

    Ponto ponto;
    std::int32_t lat = 0;
    if (!decodifica_registro(registro, &ponto, &lat)) {
        if (erro_de_registro_ == ErroBase::Nenhum) {
            erro_de_registro_ = ErroBase::RegistroInvalido;
        }
        return;
    }
    if (lat < lat_anterior_) {
        if (erro_de_registro_ == ErroBase::Nenhum) {
            erro_de_registro_ = ErroBase::ForaDeOrdem;
        }
        return;
    }
    lat_anterior_ = lat;
    // Sem destino so se conta: e o estagio de validacao da troca.
    if (destino_ != nullptr) {
        destino_[escritos_] = ponto;
    }
    ++escritos_;
}

void CarregadorFluxo::alimenta(const std::uint8_t* bytes, std::size_t tamanho) {
    if (bytes == nullptr || tamanho == 0) {
        return;
    }

    const std::size_t antes = verificador_.bytes_recebidos();
    verificador_.alimenta(bytes, tamanho);

    // Onde começa a parte deste pedaço que é dado, e não cabeçalho.
    //
    // O tamanho do cabeçalho **depende da versão** — 16 na 1, 20 na 2 —, e
    // a versão só se conhece depois que ele chega inteiro. Enquanto isso,
    // pergunta-se ao verificador, que é quem faz essa conta; duplicá-la
    // aqui criaria dois lugares para errar o mesmo deslocamento, e o
    // sintoma seria um ponto a mais ou a menos, não um erro.
    const std::size_t cab = verificador_.tem_cabecalho()
                                ? verificador_.cabecalho().tamanho()
                                : kTamCabecalho;
    std::size_t i = 0;
    if (antes < cab) {
        const std::size_t do_cabecalho = cab - antes;
        i = do_cabecalho < tamanho ? do_cabecalho : tamanho;
    }

    while (i < tamanho) {
        const std::size_t falta = kTamRegistro - parcial_n_;
        const std::size_t disponivel = tamanho - i;
        const std::size_t copiar = falta < disponivel ? falta : disponivel;
        std::memcpy(parcial_ + parcial_n_, bytes + i, copiar);
        parcial_n_ += copiar;
        i += copiar;
        if (parcial_n_ == kTamRegistro) {
            consome_registro(parcial_);
            parcial_n_ = 0;
        }
    }
}

ErroBase CarregadorFluxo::conclui() const {
    // O que o cabeçalho, a contagem e o CRC dizem vem primeiro — é a ordem do
    // `carrega_base()`, e mantê-la é o que faz os dois caminhos concordarem.
    const ErroBase pelo_formato = verificador_.conclui(capacidade_);
    if (pelo_formato != ErroBase::Nenhum) {
        return pelo_formato;
    }
    return erro_de_registro_;
}

namespace {

constexpr const char* kOrigemBase = "base";

void ao_ler_pedaco(void* contexto, const std::uint8_t* bytes,
                   std::size_t tamanho) {
    static_cast<CarregadorFluxo*>(contexto)->alimenta(bytes, tamanho);
}

/// Uma passada inteira pelo arquivo. Verdadeiro se ele leu e validou.
bool passa_pelo_arquivo(LeituraEmFluxo ler, void* fonte, const char* nome,
                        CarregadorFluxo* carregador, Logger& log) {
    char msg[96];
    carregador->reinicia();
    const ErroCartao leitura = ler(fonte, nome, ao_ler_pedaco, carregador, log);
    if (leitura != ErroCartao::Nenhum) {
        std::snprintf(msg, sizeof msg, "'%s': %s", nome, descreve(leitura));
        // Tentar o vigente e cair na reserva e o projeto funcionando. Se
        // AMBOS falharem, o ERRO de `NENHUMA base` fecha o assunto.
        log.debug(kOrigemBase, msg);
        return false;
    }
    if (carregador->conclui() != ErroBase::Nenhum) {
        std::snprintf(msg, sizeof msg, "'%s' RECUSADA", nome);
        log.error(kOrigemBase, msg);
        return false;
    }
    return true;
}

void anuncia(const char* nome, const char* reserva,
             const CarregadorFluxo& carregador, Logger& log) {
    char msg[128];
    const auto& cab = carregador.cabecalho();
    if (cab.tem_data()) {
        std::snprintf(msg, sizeof msg,
                      "%u pontos de '%s', base de %02u/%02u/%04u",
                      static_cast<unsigned>(carregador.pontos()), nome,
                      static_cast<unsigned>(cab.dia),
                      static_cast<unsigned>(cab.mes),
                      static_cast<unsigned>(cab.ano));
    } else {
        std::snprintf(msg, sizeof msg,
                      "%u pontos de '%s' (formato antigo, sem data)",
                      static_cast<unsigned>(carregador.pontos()), nome);
    }
    log.info(kOrigemBase, msg);
    if (nome == reserva) {
        log.warning(kOrigemBase, "operando pela RESERVA: atualize quando puder");
    }
}

}  // namespace

BaseCarregada troca_base(LeituraEmFluxo ler, void* fonte, const char* nome,
                         const char* reserva, Ponto* destino,
                         std::size_t capacidade, const BaseCarregada& em_uso,
                         Logger& log) {
    // Dois carregadores, um sem destino: o que valida nao tem como escrever
    // no vetor nem por engano. Custam ~100 B de pilha cada.
    CarregadorFluxo validador(nullptr, capacidade);
    CarregadorFluxo carregador(destino, capacidade);
    const bool protege = em_uso.pontos > 0;
    bool tocou_no_vetor = false;

    for (const char* candidato : {nome, reserva}) {
        if (protege &&
            !passa_pelo_arquivo(ler, fonte, candidato, &validador, log)) {
            continue;
        }
        tocou_no_vetor = true;
        if (!passa_pelo_arquivo(ler, fonte, candidato, &carregador, log)) {
            continue;
        }
        anuncia(candidato, reserva, carregador, log);
        return {carregador.pontos(), carregador.cabecalho()};
    }

    if (!tocou_no_vetor) {
        // Nada validou e nada foi escrito: a base de antes continua inteira
        // no vetor, e e ela que segue alertando.
        log.error(kOrigemBase, "nenhuma base nova validou; segue a que estava "
                               "em uso");
        return em_uso;
    }
    // RF07: sem base o aparelho opera, e precisa deixar isso evidente. Quem
    // avisa e a tela, com "BASE INDISPONIVEL".
    log.error(kOrigemBase, "NENHUMA base: nem a vigente nem a reserva");
    return {};
}

}  // namespace coruja
