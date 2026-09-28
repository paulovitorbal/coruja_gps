#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// A porta serial do receptor, vista por quem só precisa mandar e receber
/// bytes.
///
/// `define_baud` está aqui e não no porte porque **trocar o baud é parte do
/// protocolo**, não da montagem: o `CFG-PRT` do RF01.2 muda a velocidade do
/// módulo, e a resposta a esse próprio comando já vem na velocidade nova.
/// Quem conduz a configuração precisa mandar na porta.
class Uart {
public:
    virtual ~Uart() = default;

    virtual void escreve(const std::uint8_t* bytes, std::size_t tamanho) = 0;

    /// Lê o que houver, sem bloquear. Devolve quantos bytes pôs em `destino`.
    virtual std::size_t le(std::uint8_t* destino, std::size_t capacidade) = 0;

    virtual void define_baud(std::uint32_t baud) = 0;
};

}  // namespace coruja
