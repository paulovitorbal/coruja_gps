#pragma once
#include <cstddef>

#include "app/PilotoAlerta.h"
#include "nucleo/ArenaMemoria.h"
#include "nucleo/BaseRadares.h"

namespace coruja {

class Logger;

/// Quem sabe reconstruir a base de radares no buffer, do zero.
///
/// Na placa é a leitura do `radares.bin` no cartão; nos testes é um dublê.
/// Devolve quantos pontos carregou, ou zero se não conseguiu.
class RecarregadorBase {
public:
    virtual ~RecarregadorBase() = default;
    virtual std::size_t recarrega(Logger& log) = 0;
};

/// Empresta a RAM da base de radares, e **devolve o aparelho inteiro** no fim.
///
/// O handshake TLS quer dezenas de KiB que o RP2350 não tem sobrando. Ao lado
/// deles há 280 KiB parados: o vetor da base, ocioso justamente quando a rede
/// trabalha — ninguém dirige durante uma atualização.
///
/// ⚠️ **O empréstimo DESTRÓI a base.** O mbedTLS escreve por cima dela. É por
/// isso que isto é um objeto com destrutor e não um par de funções soltas:
///
/// 1. ao nascer, tira a base do `PilotoAlerta` — que passa a não alertar,
///    pelo mesmo caminho de quando não há cartão;
/// 2. entrega a área como `ArenaMemoria`, para o mbedTLS alocar dentro;
/// 3. ao morrer, **recarrega do cartão** e só então devolve o ponteiro.
///
/// Devolver o ponteiro sem recarregar seria o pior desfecho possível: o
/// piloto voltaria a alertar lendo coordenadas que são restos de um
/// handshake, e a tela não teria como denunciar isso. Deixar o piloto sem
/// base é seguro — ele simplesmente não alerta — e recarregar é o que o
/// `main.cpp` já faz depois de um OTA bem-sucedido; aqui passa a ser
/// obrigatório, inclusive quando a sessão falha.
///
/// Se a recarga falhar, o aparelho fica **sem alerta até o próximo boot**, e
/// o log diz isso em `error`. É ruim e é o lado certo: alertar com base
/// duvidosa é pior que não alertar, porque o motorista confia no que vê.
class EmprestimoDaBase {
public:
    EmprestimoDaBase(PilotoAlerta& piloto, RecarregadorBase& recarregador,
                     Ponto* base, std::size_t capacidade_em_pontos,
                     Logger& log);
    ~EmprestimoDaBase();

    EmprestimoDaBase(const EmprestimoDaBase&) = delete;
    EmprestimoDaBase& operator=(const EmprestimoDaBase&) = delete;

    /// A área emprestada, pronta para o mbedTLS alocar dentro.
    ArenaMemoria& arena() { return arena_; }

    /// Quantos bytes o empréstimo rendeu.
    std::size_t bytes() const { return arena_.capacidade(); }

private:
    PilotoAlerta&     piloto_;
    RecarregadorBase& recarregador_;
    Ponto*            base_;
    Logger&           log_;
    ArenaMemoria      arena_;
};

}  // namespace coruja
