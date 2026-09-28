#include "gps/LeitorGps.h"

namespace coruja {

void LeitorGps::reinicia() {
    usados_ = 0;
    houve_fix_ = false;
    fixes_ = 0;
    sem_fix_ = 0;
    descartadas_ = 0;
    telemetria_ = Telemetria{};
    monitor_.reinicia();
    ubx_.reinicia();
}

bool LeitorGps::tem_fix(std::uint32_t agora_ms) const {
    // Subtração em unsigned: correta através do estouro de 49 dias.
    return houve_fix_ && (agora_ms - ultimo_fix_ms_) < kFixVelhoMs;
}

void LeitorGps::processa_linha(const char* linha, std::size_t tamanho,
                               std::uint32_t agora_ms) {
    Telemetria nova;
    const ErroNmea erro = analisa_rmc(linha, tamanho, &nova);
    switch (erro) {
        case ErroNmea::Nenhum:
            telemetria_ = nova;
            ultimo_fix_ms_ = agora_ms;
            houve_fix_ = true;
            ++fixes_;
            monitor_.registra_fix(agora_ms);
            return;

        case ErroNmea::SemFix:
            // O receptor está falando e dizendo que não sabe onde está. Não
            // conta como fix, e **não** conta como erro de interpretação.
            ++sem_fix_;
            return;

        case ErroNmea::ChecksumInvalido:
        case ErroNmea::ChecksumMalformado:
            // Só isto é "estamos falhando em interpretar": a linha se
            // apresentou como sentença e não fechou. É o que o RF01.5 quer
            // contar à parte.
            monitor_.registra_checksum_invalido();
            return;

        default:
            // Sentença de outro tipo, ou bytes de UBX lidos como texto.
            // Nenhum dos dois é defeito.
            return;
    }
}

void LeitorGps::atualiza(std::uint32_t agora_ms) {
    std::uint8_t bloco[256];
    std::size_t lidos = 0;
    while ((lidos = uart_.le(bloco, sizeof bloco)) > 0) {
        for (std::size_t i = 0; i < lidos; ++i) {
            const char c = static_cast<char>(bloco[i]);
            // Todo byte vai ao leitor de UBX também: é a mesma linha.
            ubx_.consome(bloco[i]);

            if (c == '\n') {
                // O `\r` do fim fica: o `analisa_rmc` acha o `*` e para ali,
                // então o que vier depois do checksum lhe é indiferente.
                // Havia um recorte aqui, e ele era redundante — nenhum teste
                // conseguia distinguir as duas versões.
                const std::size_t tam = usados_;
                if (tam > 0) {
                    linha_[tam] = '\0';
                    processa_linha(linha_, tam, agora_ms);
                }
                usados_ = 0;
                continue;
            }
            // `$` abre sentenca: o que estava no buffer antes dele era
            // lixo. Sem isto, um quadro UBX que chegue entre duas sentencas
            // gruda na seguinte e a derruba -- e durante a configuracao do
            // modulo isso acontece o tempo todo, porque os ACK chegam no
            // meio do NMEA.
            if (c == '$') { usados_ = 0; }
            if (usados_ < kLinhaMaxima) {
                linha_[usados_++] = c;
            } else {
                // Estourou sem `\n`: não era sentença. Descarta e recomeça,
                // em vez de deixar o buffer preso para sempre.
                ++descartadas_;
                usados_ = 0;
            }
        }
        if (lidos < sizeof bloco) { break; }
    }
    // Chamado SEMPRE, inclusive sem byte nenhum: é a janela esvaziando que
    // faz a taxa cair, e sem esta chamada o silêncio nunca seria detectado.
    monitor_.avalia(agora_ms);
}

}  // namespace coruja
