#include "app/ItemCalibracao.h"

namespace coruja {

const char* nome_item(ItemCalibracao item) {
    switch (item) {
        case ItemCalibracao::Vermelho: return "vermelho";
        case ItemCalibracao::Verde:    return "verde";
        case ItemCalibracao::Azul:     return "azul";
        case ItemCalibracao::Ambar:    return "ambar  (R+G)";
        case ItemCalibracao::Rosa:     return "rosa   (R+B)";
    }
    return "?";
}

ItemCalibracao proximo(ItemCalibracao item) {
    switch (item) {
        case ItemCalibracao::Vermelho: return ItemCalibracao::Verde;
        case ItemCalibracao::Verde:    return ItemCalibracao::Azul;
        case ItemCalibracao::Azul:     return ItemCalibracao::Ambar;
        case ItemCalibracao::Ambar:    return ItemCalibracao::Rosa;
        case ItemCalibracao::Rosa:     return ItemCalibracao::Vermelho;
    }
    return ItemCalibracao::Vermelho;
}

}  // namespace coruja
