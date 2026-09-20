#pragma once
#include <cstddef>

namespace coruja {

/// O que está sendo calibrado no momento.
///
/// Os três canais isolados vêm primeiro porque servem de referência: é olhando
/// o vermelho sozinho que se julga se o âmbar puxou demais para o verde.
enum class ItemCalibracao {
    Vermelho,
    Verde,
    Azul,
    Ambar,  ///< vermelho + verde
    Rosa,   ///< vermelho + azul
};

constexpr std::size_t kQuantosItens = 5;

const char* nome_item(ItemCalibracao item);

/// Próximo item, circular.
ItemCalibracao proximo(ItemCalibracao item);

/// Verdadeiro nos itens em que o vermelho fica fixo em 100% e o encoder ajusta
/// o **outro** canal. É essa razão que vai para o `led/Calibracao.h`.
constexpr bool e_composta(ItemCalibracao item) {
    return item == ItemCalibracao::Ambar || item == ItemCalibracao::Rosa;
}

}  // namespace coruja
