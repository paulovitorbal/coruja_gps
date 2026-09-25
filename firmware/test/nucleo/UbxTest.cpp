#include "nucleo/Ubx.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace {

using namespace coruja::ubx;

/// Quadros de referência **calculados fora deste código**, por uma
/// implementação independente em Python. Não são a saída do `monta()`
/// copiada de volta: se fossem, o teste concordaria com qualquer erro que o
/// `monta()` cometesse de forma consistente — inclusive somar o checksum
/// sobre a faixa errada de bytes. Foi assim que os checksums NMEA
/// fabricados à mão escaparam da primeira versão daquele parser.
const std::vector<std::uint8_t> kCfgRate250 = {
    0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0xFA, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x10, 0x96};
const std::vector<std::uint8_t> kCfgMsgDesligaGga = {
    0xB5, 0x62, 0x06, 0x01, 0x03, 0x00, 0xF0, 0x00, 0x00, 0xFA, 0x0F};
const std::vector<std::uint8_t> kCfgMsgLigaRmc = {
    0xB5, 0x62, 0x06, 0x01, 0x03, 0x00, 0xF0, 0x04, 0x01, 0xFF, 0x18};
const std::vector<std::uint8_t> kCfgPrt115200 = {
    0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00,
    0xD0, 0x08, 0x00, 0x00, 0x00, 0xC2, 0x01, 0x00, 0x03, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBC, 0x5E};
const std::vector<std::uint8_t> kCfgCfgSalva = {
    0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x1F, 0xAD};
const std::vector<std::uint8_t> kAckDeCfgRate = {
    0xB5, 0x62, 0x05, 0x01, 0x02, 0x00, 0x06, 0x08, 0x16, 0x3F};
const std::vector<std::uint8_t> kNakDeCfgPrt = {
    0xB5, 0x62, 0x05, 0x00, 0x02, 0x00, 0x06, 0x00, 0x0D, 0x32};

std::vector<std::uint8_t> montado(std::size_t (*f)(std::uint8_t*, std::size_t)) {
    std::uint8_t buf[64] = {};
    const std::size_t n = f(buf, sizeof buf);
    return std::vector<std::uint8_t>(buf, buf + n);
}

/// Entrega uma sequência ao leitor e devolve o último evento não-`Nada`.
EventoUbx alimenta(LeitorUbx* leitor, const std::vector<std::uint8_t>& bytes) {
    EventoUbx ultimo = EventoUbx::Nada;
    for (const std::uint8_t b : bytes) {
        const EventoUbx e = leitor->consome(b);
        if (e != EventoUbx::Nada) { ultimo = e; }
    }
    return ultimo;
}

// ======================================================= montagem de quadro

TEST(UbxMonta, cfg_rate_bate_com_a_referencia_externa) {
    EXPECT_EQ(montado([](std::uint8_t* d, std::size_t c) {
                  return monta_cfg_rate(250, d, c); }),
              kCfgRate250);
}

TEST(UbxMonta, cfg_msg_desliga_e_liga) {
    EXPECT_EQ(montado([](std::uint8_t* d, std::size_t c) {
                  return monta_cfg_msg(kClasseNmea, kNmeaGga, 0, d, c); }),
              kCfgMsgDesligaGga);
    EXPECT_EQ(montado([](std::uint8_t* d, std::size_t c) {
                  return monta_cfg_msg(kClasseNmea, kNmeaRmc, 1, d, c); }),
              kCfgMsgLigaRmc);
}

TEST(UbxMonta, cfg_prt_uart_a_115200) {
    EXPECT_EQ(montado([](std::uint8_t* d, std::size_t c) {
                  return monta_cfg_prt_uart(115200, d, c); }),
              kCfgPrt115200);
}

TEST(UbxMonta, cfg_cfg_persiste_em_bbr_e_eeprom) {
    EXPECT_EQ(montado([](std::uint8_t* d, std::size_t c) {
                  return monta_cfg_cfg(0xFFFFU, kDispBbr | kDispEeprom, d, c); }),
              kCfgCfgSalva);
}

TEST(UbxMonta, tamanho_e_little_endian_no_campo_de_comprimento) {
    // Payload de 260 bytes: exercita o byte alto, que um formato só de um
    // byte engoliria em silêncio.
    std::uint8_t payload[260] = {};
    std::uint8_t buf[300] = {};
    const std::size_t n = monta(0x0A, 0x04, payload, sizeof payload, buf,
                                sizeof buf);
    ASSERT_EQ(n, sizeof payload + kSobrecarga);
    EXPECT_EQ(buf[4], 0x04);   // 260 = 0x0104
    EXPECT_EQ(buf[5], 0x01);
}

TEST(UbxMonta, recusa_sem_escrever_quando_nao_cabe) {
    std::uint8_t buf[8];
    std::memset(buf, 0xEE, sizeof buf);
    const std::uint8_t p[4] = {1, 2, 3, 4};
    // 4 de payload + 8 de sobrecarga = 12 > 8.
    EXPECT_EQ(monta(0x06, 0x08, p, sizeof p, buf, sizeof buf), 0U);
    for (const std::uint8_t b : buf) {
        EXPECT_EQ(b, 0xEE) << "escreveu parcialmente: um quadro pela metade "
                              "na UART e' pior que quadro nenhum";
    }
}

TEST(UbxMonta, cabe_exatamente_na_capacidade_justa) {
    std::uint8_t buf[kSobrecarga + 3];
    const std::uint8_t p[3] = {0xF0, 0x04, 0x01};
    EXPECT_EQ(monta(0x06, 0x01, p, sizeof p, buf, sizeof buf), sizeof buf);
}

TEST(UbxMonta, destino_nulo_nao_estoura) {
    EXPECT_EQ(monta_cfg_rate(250, nullptr, 64), 0U);
}

TEST(UbxChecksum, nao_cobre_os_bytes_de_sincronismo) {
    // Se o checksum incluísse o sync, este valor seria outro. A referência
    // externa já cobre isso, mas aqui fica explícito por quê.
    std::uint8_t a = 0;
    std::uint8_t b = 0;
    checksum(&kCfgRate250[2], kCfgRate250.size() - 4, &a, &b);
    EXPECT_EQ(a, 0x10);
    EXPECT_EQ(b, 0x96);
}

// ============================================================ leitura de ACK

TEST(UbxLeitor, reconhece_ack_e_diz_de_qual_mensagem) {
    LeitorUbx l;
    EXPECT_EQ(alimenta(&l, kAckDeCfgRate), EventoUbx::Ack);
    EXPECT_EQ(l.classe_alvo(), kClasseCfg);
    EXPECT_EQ(l.id_alvo(), kCfgRate);
}

TEST(UbxLeitor, reconhece_nak) {
    LeitorUbx l;
    EXPECT_EQ(alimenta(&l, kNakDeCfgPrt), EventoUbx::Nak);
    EXPECT_EQ(l.classe_alvo(), kClasseCfg);
    EXPECT_EQ(l.id_alvo(), kCfgPrt);
}

TEST(UbxLeitor, ack_e_nak_nao_se_confundem) {
    // Os dois diferem por UM byte, o id. Trocá-los inverteria o significado
    // de "o modulo aceitou" sem mudar mais nada no quadro.
    LeitorUbx l;
    ASSERT_EQ(alimenta(&l, kAckDeCfgRate), EventoUbx::Ack);
    EXPECT_EQ(alimenta(&l, kNakDeCfgPrt), EventoUbx::Nak);
}

TEST(UbxLeitor, checksum_errado_e_recusado_e_contado) {
    std::vector<std::uint8_t> ruim = kAckDeCfgRate;
    ruim.back() ^= 0xFF;
    LeitorUbx l;
    EXPECT_EQ(alimenta(&l, ruim), EventoUbx::ChecksumInvalido);
    EXPECT_EQ(l.quadros_invalidos(), 1U);
}

TEST(UbxLeitor, erro_no_primeiro_byte_de_checksum_tambem_e_pego) {
    std::vector<std::uint8_t> ruim = kAckDeCfgRate;
    ruim[ruim.size() - 2] ^= 0x01;
    LeitorUbx l;
    EXPECT_EQ(alimenta(&l, ruim), EventoUbx::ChecksumInvalido);
}

TEST(UbxLeitor, acha_o_quadro_no_meio_de_nmea) {
    // O caso real: UBX e NMEA dividem a mesma UART.
    const char* antes = "$GNRMC,123519.00,A,1947.99496,S,0440";
    const char* depois = "1.26264,W,22.4,84.4,220926,,,A*46\r\n";
    LeitorUbx l;
    for (const char* p = antes; *p != 0; ++p) {
        ASSERT_EQ(l.consome(static_cast<std::uint8_t>(*p)), EventoUbx::Nada);
    }
    EXPECT_EQ(alimenta(&l, kAckDeCfgRate), EventoUbx::Ack);
    for (const char* p = depois; *p != 0; ++p) {
        EXPECT_EQ(l.consome(static_cast<std::uint8_t>(*p)), EventoUbx::Nada);
    }
}

TEST(UbxLeitor, sync1_repetido_nao_perde_o_quadro) {
    // `B5 B5 62 ...`: o primeiro B5 é lixo, o segundo abre o quadro. Voltar
    // cegamente ao estado inicial ao ver um byte inesperado perderia este.
    std::vector<std::uint8_t> com_ruido = {0xB5};
    com_ruido.insert(com_ruido.end(), kAckDeCfgRate.begin(),
                     kAckDeCfgRate.end());
    LeitorUbx l;
    EXPECT_EQ(alimenta(&l, com_ruido), EventoUbx::Ack);
}

TEST(UbxLeitor, tamanho_absurdo_faz_ressincronizar_em_vez_de_engolir) {
    // `B5 62` casual no meio de dados, com um "tamanho" de 60 KiB. Seguir em
    // frente descartaria todos os quadros bons dos proximos 60 KiB.
    std::vector<std::uint8_t> lixo = {0xB5, 0x62, 0x01, 0x02, 0xFF, 0xEF};
    LeitorUbx l;
    EXPECT_EQ(alimenta(&l, lixo), EventoUbx::QuadroLongoDemais);
    EXPECT_EQ(alimenta(&l, kAckDeCfgRate), EventoUbx::Ack);
}

TEST(UbxLeitor, quadro_de_payload_vazio) {
    std::uint8_t buf[16];
    const std::size_t n = monta(0x0A, 0x02, nullptr, 0, buf, sizeof buf);
    ASSERT_EQ(n, kSobrecarga);
    LeitorUbx l;
    EXPECT_EQ(alimenta(&l, std::vector<std::uint8_t>(buf, buf + n)),
              EventoUbx::Outro);
    EXPECT_EQ(l.classe(), 0x0A);
    EXPECT_EQ(l.id(), 0x02);
}

TEST(UbxLeitor, payload_maior_que_o_buffer_ainda_valida_o_checksum) {
    // O leitor guarda 8 bytes e descarta o resto, mas TEM de somar tudo:
    // senão um quadro longo corrompido passaria como bom.
    std::uint8_t payload[64];
    for (std::size_t i = 0; i < sizeof payload; ++i) {
        payload[i] = static_cast<std::uint8_t>(i * 7U);
    }
    std::uint8_t buf[128];
    const std::size_t n = monta(0x01, 0x07, payload, sizeof payload, buf,
                                sizeof buf);
    std::vector<std::uint8_t> bom(buf, buf + n);
    LeitorUbx l;
    EXPECT_EQ(alimenta(&l, bom), EventoUbx::Outro);

    std::vector<std::uint8_t> corrompido = bom;
    corrompido[40] ^= 0x01;   // bem depois dos 8 bytes guardados
    LeitorUbx l2;
    EXPECT_EQ(alimenta(&l2, corrompido), EventoUbx::ChecksumInvalido);
}

TEST(UbxLeitor, quadro_que_nao_e_ack_nao_vira_ack) {
    std::uint8_t buf[32];
    const std::uint8_t p[2] = {kClasseCfg, kCfgRate};
    // Mesma carga de um ACK, mas classe 0x06 em vez de 0x05.
    const std::size_t n = monta(kClasseCfg, kAckAck, p, sizeof p, buf,
                                sizeof buf);
    LeitorUbx l;
    EXPECT_EQ(alimenta(&l, std::vector<std::uint8_t>(buf, buf + n)),
              EventoUbx::Outro);
}

TEST(UbxLeitor, ack_com_tamanho_errado_nao_e_aceito_como_ack) {
    std::uint8_t buf[32];
    const std::uint8_t p[3] = {kClasseCfg, kCfgRate, 0x00};
    const std::size_t n = monta(kClasseAck, kAckAck, p, sizeof p, buf,
                                sizeof buf);
    LeitorUbx l;
    EXPECT_EQ(alimenta(&l, std::vector<std::uint8_t>(buf, buf + n)),
              EventoUbx::Outro);
}

TEST(UbxLeitor, um_quadro_truncado_engole_o_seguinte_e_o_terceiro_e_lido) {
    // Propriedade real, não defeito: truncado no meio do payload, o leitor
    // continua contando bytes e consome o cabeçalho do próximo quadro como
    // se fosse carga. O checksum então falha, ele ressincroniza, e o quadro
    // DEPOIS desse é lido. Sem um relógio para expirar o quadro pendente não
    // há como fazer melhor, e um byte-timeout seria complexidade especulativa
    // para um caso que só aparece com perda de bytes na UART.
    std::vector<std::uint8_t> truncado(kAckDeCfgRate.begin(),
                                       kAckDeCfgRate.begin() + 6);
    LeitorUbx l;
    alimenta(&l, truncado);
    EXPECT_EQ(alimenta(&l, kAckDeCfgRate), EventoUbx::ChecksumInvalido)
        << "o quadro seguinte e' consumido como carga do truncado";
    EXPECT_EQ(alimenta(&l, kAckDeCfgRate), EventoUbx::Ack)
        << "e o terceiro volta ao normal";
}

TEST(UbxLeitor, ida_e_volta_de_toda_a_sequencia_do_rf01_2) {
    struct { std::size_t (*monta)(std::uint8_t*, std::size_t); std::uint8_t id; }
    const passos[] = {
        {[](std::uint8_t* d, std::size_t c) { return monta_cfg_rate(250, d, c); }, kCfgRate},
        {[](std::uint8_t* d, std::size_t c) { return monta_cfg_msg(kClasseNmea, kNmeaGsv, 0, d, c); }, kCfgMsg},
        {[](std::uint8_t* d, std::size_t c) { return monta_cfg_prt_uart(115200, d, c); }, kCfgPrt},
        {[](std::uint8_t* d, std::size_t c) { return monta_cfg_cfg(0xFFFFU, kDispBbr, d, c); }, kCfgCfg},
    };
    for (const auto& passo : passos) {
        std::uint8_t buf[64];
        const std::size_t n = passo.monta(buf, sizeof buf);
        ASSERT_GT(n, 0U);
        LeitorUbx l;
        EXPECT_EQ(alimenta(&l, std::vector<std::uint8_t>(buf, buf + n)),
                  EventoUbx::Outro);
        EXPECT_EQ(l.classe(), kClasseCfg);
        EXPECT_EQ(l.id(), passo.id);
    }
}

TEST(UbxLeitor, descricoes_cobrem_todos_os_eventos) {
    for (const EventoUbx e : {EventoUbx::Nada, EventoUbx::Ack, EventoUbx::Nak,
                              EventoUbx::Outro, EventoUbx::ChecksumInvalido,
                              EventoUbx::QuadroLongoDemais}) {
        EXPECT_STRNE(descreve(e), "?");
    }
}

}  // namespace
