#include "gps/LeitorGps.h"

namespace coruja {

void LeitorGps::reinicia() {
    usados_ = 0;
    houve_fix_ = false;
    fixes_ = 0;
    sem_fix_ = 0;
    descartadas_ = 0;
    telemetria_ = Telemetria{};
    qualidade_ = QualidadeFix{};
    tem_qualidade_ = false;
    navegacao_ = EstadoNavegacao{};
    radio_ = EstadoRadio{};
    aop_ = EstadoAop{};
    diagnosticos_ = 0;
    monitor_.reinicia();
    ubx_.reinicia();
}

void LeitorGps::pergunta_diagnostico() {
    // As três de uma vez: elas respondem juntas a mesma pergunta, e separar
    // em três momentos só daria três instantes diferentes para comparar.
    const struct { std::uint8_t classe, id; } consultas[] = {
        {ubx::kClasseNav, ubx::kNavStatus},
        {ubx::kClasseMon, ubx::kMonHw},
        {ubx::kClasseNav, ubx::kNavAopStatus},
        // ⚠️ A mais cara das quatro: com o ceu cheio o quadro passa de 300
        // bytes, e o leitor guarda 248. O resumo sai dos que couberam e o
        // total vem do cabecalho -- ver `analisa_nav_sat`.
        {ubx::kClasseNav, ubx::kNavSat},
    };
    std::uint8_t quadro[ubx::kSobrecarga];
    for (const auto& c : consultas) {
        const std::size_t n =
            ubx::monta_consulta(c.classe, c.id, quadro, sizeof quadro);
        if (n > 0) { uart_.escreve(quadro, n); }
    }
}

void LeitorGps::guarda_diagnostico() {
    const std::uint8_t* p = ubx_.payload();
    const std::size_t tam = ubx_.tamanho_payload();
    bool decodificou = false;
    if (ubx_.classe() == ubx::kClasseNav && ubx_.id() == ubx::kNavStatus) {
        decodificou = analisa_nav_status(p, tam, &navegacao_);
    } else if (ubx_.classe() == ubx::kClasseMon && ubx_.id() == ubx::kMonHw) {
        decodificou = analisa_mon_hw(p, tam, &radio_);
    } else if (ubx_.classe() == ubx::kClasseNav &&
               ubx_.id() == ubx::kNavAopStatus) {
        decodificou = analisa_nav_aopstatus(p, tam, &aop_);
    } else if (ubx_.classe() == ubx::kClasseNav && ubx_.id() == ubx::kNavSat) {
        decodificou = analisa_nav_sat(p, tam, &sinal_);
    }
    // Só conta o que foi entendido. Um quadro de outra classe, ou curto
    // demais, não é resposta à pergunta que alguém fez — e quem espera a
    // resposta ficaria achando que ela chegou.
    if (decodificou) { ++diagnosticos_; }
}

bool LeitorGps::tem_qualidade(std::uint32_t agora_ms) const {
    // Mesma janela do fix, e pelo mesmo motivo: passado esse tempo o número
    // descreve um instante que já não é o atual.
    return tem_qualidade_ && (agora_ms - qualidade_ms_) < kFixVelhoMs;
}

void LeitorGps::processa_gga(const char* linha, std::size_t tamanho,
                             std::uint32_t agora_ms) {
    QualidadeFix nova;
    if (analisa_gga(linha, tamanho, &nova) != ErroNmea::Nenhum) {
        // Inclui `SemFix`: o receptor dizendo que ainda não tem solução não
        // pode apagar a última qualidade conhecida.
        return;
    }
    qualidade_ = nova;
    qualidade_ms_ = agora_ms;
    tem_qualidade_ = true;
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
            //
            // O monitor precisa saber disto, e não só da ausência de fix:
            // para a taxa, o túnel e o módulo mudo são a mesma coisa, e é
            // esta linha que os separa na hora de decidir se vale reenviar
            // a configuração (RF01.5).
            ++sem_fix_;
            monitor_.registra_sem_fix(agora_ms);
            return;

        case ErroNmea::ChecksumInvalido:
        case ErroNmea::ChecksumMalformado:
            // Só isto é "estamos falhando em interpretar": a linha se
            // apresentou como sentença e não fechou. É o que o RF01.5 quer
            // contar à parte.
            monitor_.registra_checksum_invalido();
            return;

        case ErroNmea::NaoEhRmc:
            // Pode ser a GGA, que é de onde vêm os satélites e o HDOP. O
            // checksum já foi validado acima para QUALQUER sentença, então
            // o que chega aqui está íntegro.
            processa_gga(linha, tamanho, agora_ms);
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
            if (ubx_.consome(bloco[i]) == ubx::EventoUbx::Outro) {
                guarda_diagnostico();
            }

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
