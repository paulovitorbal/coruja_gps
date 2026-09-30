#pragma once
#include <cstdint>
#include <vector>

#include "nucleo/BaseRadares.h"
#include "nucleo/Crc32.h"

namespace coruja {
namespace apoio {

/// Monta um `radares.bin` valido em memoria, para que cada teste corrompa um
/// campo de cada vez e confirme que a validacao pega exatamente aquele.
///
/// Vive aqui, e nao dentro de um arquivo de teste, porque os DOIS caminhos de
/// leitura precisam dele: o `carrega_base()`, que le o arquivo inteiro, e o
/// `VerificadorDownload`, que o verifica em fluxo. Ter duas copias do
/// construtor abriria a porta para elas divergirem -- e seria justo no
/// cabecalho, que e onde os dois caminhos tem de concordar.
class ConstrutorBase {
public:
    void adiciona(std::int32_t lat_e, std::int32_t lon_e, std::uint8_t limite,
                  std::uint8_t rumo_q, std::uint8_t tipo, std::uint8_t sentido) {
        const std::uint8_t flags =
            static_cast<std::uint8_t>((sentido & 0x03U) | ((tipo & 0x07U) << 2));
        escreve_i32(registros_, lat_e);
        escreve_i32(registros_, lon_e);
        registros_.push_back(limite);
        registros_.push_back(rumo_q);
        registros_.push_back(flags);
        registros_.push_back(0);
    }

    /// Monta um arquivo. `versao` permite construir o formato ANTIGO, que
    /// o firmware continua aceitando: um cartao com base v1 vale, e o
    /// aparelho so fica sem a data para mostrar.
    std::vector<std::uint8_t> constroi(std::uint16_t versao = kVersao,
                                       std::uint16_t ano = 2026,
                                       std::uint8_t mes = 9,
                                       std::uint8_t dia = 30) const {
        std::vector<std::uint8_t> out;
        escreve_u32(out, kMagic);
        escreve_u16(out, versao);
        out.push_back(kExpoenteEscala);
        out.push_back(kTamRegistro);
        escreve_u32(out,
                    static_cast<std::uint32_t>(registros_.size() / kTamRegistro));
        escreve_u32(out, crc32(registros_.data(), registros_.size()));
        if (versao != kVersaoSemData) {
            escreve_u16(out, ano);
            out.push_back(mes);
            out.push_back(dia);
        }
        out.insert(out.end(), registros_.begin(), registros_.end());
        return out;
    }

private:
    static void escreve_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
        v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
        v.push_back((x >> 16) & 0xFF); v.push_back((x >> 24) & 0xFF);
    }
    static void escreve_u16(std::vector<std::uint8_t>& v, std::uint16_t x) {
        v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
    }
    static void escreve_i32(std::vector<std::uint8_t>& v, std::int32_t x) {
        escreve_u32(v, static_cast<std::uint32_t>(x));
    }
    std::vector<std::uint8_t> registros_;
};

inline ConstrutorBase tres_pontos_validos() {
    ConstrutorBase c;
    c.adiciona(-1600000, -4800000, 60, 45, 1, 1);
    c.adiciona(-1578010, -4792920, 0, 0, 3, 0);
    c.adiciona(-1500000, -4790000, 110, 90, 5, 2);
    return c;
}

}  // namespace apoio
}  // namespace coruja
