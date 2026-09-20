#include "app/ModoCalibracao.h"

namespace coruja {

namespace {

/// Soma saturando em 255 e em 0. Sem isto, girar além do fim daria a volta e
/// o brilho saltaria de máximo para mínimo num detente — o oposto do que a
/// mão espera de um ajuste contínuo.
std::uint8_t soma_limitada(std::uint8_t atual, int passo) {
    const int novo = static_cast<int>(atual) + passo;
    if (novo < 0) {
        return 0;
    }
    if (novo > 255) {
        return 255;
    }
    return static_cast<std::uint8_t>(novo);
}

}  // namespace

std::uint8_t& ModoCalibracao::variavel(ItemCalibracao item) {
    switch (item) {
        case ItemCalibracao::Vermelho: return vermelho_;
        case ItemCalibracao::Verde:    return verde_;
        case ItemCalibracao::Azul:     return azul_;
        case ItemCalibracao::Ambar:    return ambar_verde_;
        case ItemCalibracao::Rosa:     return rosa_azul_;
    }
    return vermelho_;
}

const std::uint8_t& ModoCalibracao::variavel(ItemCalibracao item) const {
    return const_cast<ModoCalibracao*>(this)->variavel(item);
}

std::uint8_t ModoCalibracao::duty(ItemCalibracao item) const {
    return variavel(item);
}

float ModoCalibracao::razao(ItemCalibracao item) const {
    return static_cast<float>(variavel(item)) / 255.0F;
}

Cor ModoCalibracao::cor() const {
    switch (item_) {
        case ItemCalibracao::Vermelho: return Cor{vermelho_, 0, 0};
        case ItemCalibracao::Verde:    return Cor{0, verde_, 0};
        case ItemCalibracao::Azul:     return Cor{0, 0, azul_};
        // Nas compostas o vermelho fica em 100% e o encoder move o outro
        // canal: é a forma como led/Calibracao.h guarda as cores.
        case ItemCalibracao::Ambar:    return Cor{255, ambar_verde_, 0};
        case ItemCalibracao::Rosa:     return Cor{255, 0, rosa_azul_};
    }
    return cores::kApagado;
}

Cor ModoCalibracao::aplica(EventoEncoder evento) {
    completou_ciclo_ = false;

    switch (evento) {
        case EventoEncoder::GiroDireita:
            variavel(item_) = soma_limitada(variavel(item_), kPasso);
            break;
        case EventoEncoder::GiroEsquerda:
            variavel(item_) = soma_limitada(variavel(item_), -kPasso);
            break;
        case EventoEncoder::Clique:
            item_ = proximo(item_);
            completou_ciclo_ = (item_ == ItemCalibracao::Vermelho);
            break;
        case EventoEncoder::Nenhum:
            break;
    }
    return cor();
}

}  // namespace coruja
