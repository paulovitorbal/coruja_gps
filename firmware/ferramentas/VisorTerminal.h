#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "display/Visor.h"

namespace coruja {

/// O painel de 2,4" desenhado em ANSI, no terminal.
///
/// **Não é maquete.** Quem desenha é a `TelaPrincipal` de verdade, com a
/// geometria de verdade — os 320×240 são mapeados numa grade de caracteres,
/// e cada `retangulo`, `texto` e `icone` cai onde cairia no painel. O que
/// muda é só o dispositivo de saída.
///
/// Serve ao que teste unitário não alcança: ver a coisa **em movimento**. A
/// tela tremulando ao trocar de estado, a barra que não enche, o número
/// piscando ao mudar de alvo — nada disso aparece numa asserção, e os três
/// últimos defeitos desta camada apareceram justamente assim.
class VisorTerminal : public Visor {
public:
    static constexpr int kColunas = 80;
    static constexpr int kLinhas = 24;

    VisorTerminal();

    void retangulo(int x, int y, int largura, int altura, Cor565 cor) override;
    void texto(int x, int y, const char* texto, Fonte fonte, Cor565 cor,
               Alinhamento alinhamento) override;
    void icone(int x, int y, Icone icone) override;
    void apresenta() override;

    /// Linha de estado impressa abaixo do painel: LED, buzzer e taxa.
    void define_rodape(const std::string& s) { rodape_ = s; }

private:
    /// O glifo é `std::string` e não `char` por causa dos emoji: eles têm
    /// vários bytes e ocupam **duas** colunas no terminal. A célula seguinte
    /// vira continuação e não imprime nada, senão tudo à direita escorrega.
    struct Celula {
        std::string   glifo = " ";
        std::uint32_t frente = 0x808080;
        std::uint32_t fundo = 0x000000;
        bool          continuacao = false;
    };

    static int coluna_de(int x);
    static int linha_de(int y);
    void escreve(int coluna, int linha, const std::string& g,
                 std::uint32_t cor, int largura = 1);
    void escreve_texto(int coluna, int linha, const std::string& s,
                       std::uint32_t cor);
    void numero_grande(int centro_coluna, int linha, const std::string& s,
                       std::uint32_t cor);
    static int largura_em_colunas(const std::string& s);

    std::vector<Celula> grade_;
    std::string         rodape_;
    bool                primeira_ = true;
};

}  // namespace coruja
