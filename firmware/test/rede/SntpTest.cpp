#include "rede/Sntp.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "nucleo/TempoUtc.h"

namespace {

using namespace coruja;

using Pacote = std::array<std::uint8_t, kTamanhoPacoteNtp>;

/// Uma resposta de servidor crível: modo 4, estrato 2, sem aviso de salto.
Pacote resposta(std::uint32_t transmissao) {
    Pacote p{};
    p[0] = (4U << 3) | 4U;   // LI=0, VN=4, Mode=4 (servidor)
    p[1] = 2;                // estrato 2
    p[40] = static_cast<std::uint8_t>(transmissao >> 24);
    p[41] = static_cast<std::uint8_t>(transmissao >> 16);
    p[42] = static_cast<std::uint8_t>(transmissao >> 8);
    p[43] = static_cast<std::uint8_t>(transmissao);
    return p;
}

/// O instante, em segundos de NTP (era 0).
std::uint32_t ntp_de(std::int64_t unix) {
    return static_cast<std::uint32_t>(unix + kNtpParaUnix);
}

std::int64_t lido(const Pacote& p, ErroNtp esperado = ErroNtp::Nenhum) {
    std::int64_t s = -1;
    EXPECT_EQ(le_resposta_ntp(p.data(), p.size(), &s), esperado);
    return s;
}

// =============================================================== o pedido

TEST(PedidoNtp, tem_48_bytes_com_versao_4_e_modo_cliente) {
    std::uint8_t p[kTamanhoPacoteNtp];
    std::memset(p, 0xAA, sizeof p);
    monta_pedido_ntp(p);

    EXPECT_EQ(p[0] & 0x07U, 3U) << "modo 3 = cliente";
    EXPECT_EQ((p[0] >> 3) & 0x07U, 4U) << "versao 4";
    EXPECT_EQ((p[0] >> 6) & 0x03U, 0U) << "sem aviso de salto";
    // O resto é zero: um cliente simples não preenche mais nada, e campo
    // inventado só daria ao servidor motivo para recusar.
    for (std::size_t i = 1; i < kTamanhoPacoteNtp; ++i) {
        EXPECT_EQ(p[i], 0) << "byte " << i;
    }
}

TEST(PedidoNtp, ponteiro_nulo_nao_quebra) {
    monta_pedido_ntp(nullptr);
}

// ============================================================== a resposta

TEST(RespostaNtp, le_a_hora_de_transmissao) {
    const TempoUtc quando{2026, 10, 6, 14, 30, 0};
    const std::int64_t esperado = segundos_de(quando);
    EXPECT_EQ(lido(resposta(ntp_de(esperado))), esperado);
}

TEST(RespostaNtp, a_epoca_do_ntp_e_1900_e_nao_1970) {
    // Setenta anos com dezessete bissextos. Errar esta constante dá um
    // relógio setenta anos no passado, e todo certificado "ainda não é
    // válido".
    EXPECT_EQ(kNtpParaUnix, 2208988800LL);

    // 2026-01-01T00:00:00Z em segundos de NTP, conferido pela própria
    // constante e não por um número que eu tenha escrito à mão.
    const std::int64_t unix = segundos_de(TempoUtc{2026, 1, 1, 0, 0, 0});
    EXPECT_EQ(lido(resposta(ntp_de(unix))), unix);
}

TEST(RespostaNtp, a_parte_fracionaria_e_ignorada_e_nao_atrapalha) {
    const std::int64_t unix = segundos_de(TempoUtc{2026, 10, 6, 14, 30, 0});
    Pacote p = resposta(ntp_de(unix));
    p[44] = 0xFF;  // fração: meio segundo e pico
    p[45] = 0xFF;
    EXPECT_EQ(lido(p), unix);
}

// ====================================================== a virada de 2036

TEST(RespostaNtp, depois_de_2036_a_era_1_nao_volta_para_1900) {
    // O contador de 32 bits do NTP estoura em 2036-02-07. Um cliente que
    // ignorasse isso saltaria de 2036 para 1900 de uma vez -- e aí NENHUM
    // certificado valida.
    const std::int64_t unix_2040 = segundos_de(TempoUtc{2040, 6, 1, 12, 0, 0});
    const std::uint32_t bruto =
        static_cast<std::uint32_t>(unix_2040 + kNtpParaUnix);  // já estourou
    ASSERT_EQ(bruto & 0x80000000U, 0U) << "em 2040 o bit alto está zerado";

    EXPECT_EQ(lido(resposta(bruto)), unix_2040);
}

TEST(RespostaNtp, antes_de_2036_continua_na_era_zero) {
    const std::int64_t unix = segundos_de(TempoUtc{2026, 10, 6, 0, 0, 0});
    const auto bruto = static_cast<std::uint32_t>(unix + kNtpParaUnix);
    ASSERT_NE(bruto & 0x80000000U, 0U) << "em 2026 o bit alto está ligado";
    EXPECT_EQ(lido(resposta(bruto)), unix);
}

// ================================================== o que não se acredita

TEST(RespostaNtp, pacote_curto_e_recusado) {
    Pacote p = resposta(ntp_de(segundos_de(TempoUtc{2026, 10, 6})));
    std::int64_t s = 0;
    EXPECT_EQ(le_resposta_ntp(p.data(), 47, &s), ErroNtp::TamanhoInvalido);
    EXPECT_EQ(le_resposta_ntp(p.data(), 0, &s), ErroNtp::TamanhoInvalido);
    EXPECT_EQ(le_resposta_ntp(nullptr, 48, &s), ErroNtp::TamanhoInvalido);
    EXPECT_EQ(le_resposta_ntp(p.data(), 48, nullptr), ErroNtp::TamanhoInvalido);
}

TEST(RespostaNtp, o_proprio_pedido_de_volta_nao_e_resposta) {
    // Modo 3 é cliente. Um pacote ecoado -- por engano ou de propósito --
    // não pode virar hora.
    Pacote p = resposta(ntp_de(segundos_de(TempoUtc{2026, 10, 6})));
    p[0] = (4U << 3) | 3U;
    lido(p, ErroNtp::NaoEhResposta);
}

TEST(RespostaNtp, servidor_que_se_declara_sem_sincronia_e_recusado) {
    // Indicador de salto 3: "meu relógio não está acertado". É o que um
    // servidor responde logo depois de subir, e a hora dele não vale nada.
    Pacote p = resposta(ntp_de(segundos_de(TempoUtc{2026, 10, 6})));
    p[0] = static_cast<std::uint8_t>((3U << 6) | (4U << 3) | 4U);
    lido(p, ErroNtp::NaoSincronizado);
}

TEST(RespostaNtp, kiss_o_death_e_recusado) {
    // Estrato 0: o servidor está pedindo para o cliente parar de incomodar,
    // e o pacote carrega um código de texto no lugar de hora.
    Pacote p = resposta(ntp_de(segundos_de(TempoUtc{2026, 10, 6})));
    p[1] = 0;
    lido(p, ErroNtp::EstratoInvalido);
}

TEST(RespostaNtp, estrato_acima_de_15_e_recusado) {
    Pacote p = resposta(ntp_de(segundos_de(TempoUtc{2026, 10, 6})));
    p[1] = 16;
    lido(p, ErroNtp::EstratoInvalido);
    p[1] = 255;
    lido(p, ErroNtp::EstratoInvalido);
}

TEST(RespostaNtp, estratos_validos_passam) {
    for (std::uint8_t e = 1; e <= 15; ++e) {
        Pacote p = resposta(ntp_de(segundos_de(TempoUtc{2026, 10, 6})));
        p[1] = e;
        std::int64_t s = 0;
        EXPECT_EQ(le_resposta_ntp(p.data(), p.size(), &s), ErroNtp::Nenhum)
            << "estrato " << int(e);
    }
}

TEST(RespostaNtp, hora_de_transmissao_zerada_e_recusada) {
    lido(resposta(0), ErroNtp::TimestampZero);
}

TEST(RespostaNtp, hora_implausivel_e_recusada) {
    // 1990 em segundos de NTP: pacote bem formado, hora que este aparelho
    // não pode ter vivido. Aceitar poria o relógio -- e a validação de
    // certificado -- na mão do primeiro pacote que chegasse.
    const std::int64_t unix_1990 = segundos_de(TempoUtc{1990, 1, 1, 0, 0, 0});
    lido(resposta(static_cast<std::uint32_t>(unix_1990 + kNtpParaUnix)),
         ErroNtp::ForaDeFaixa);
}

TEST(RespostaNtp, recusa_nao_toca_no_destino) {
    std::int64_t s = 12345;
    Pacote p = resposta(0);
    le_resposta_ntp(p.data(), p.size(), &s);
    EXPECT_EQ(s, 12345);
}

TEST(RespostaNtp, todo_erro_tem_texto_proprio) {
    for (auto e : {ErroNtp::Nenhum, ErroNtp::TamanhoInvalido,
                   ErroNtp::NaoEhResposta, ErroNtp::NaoSincronizado,
                   ErroNtp::EstratoInvalido, ErroNtp::TimestampZero,
                   ErroNtp::ForaDeFaixa}) {
        EXPECT_STRNE(descreve(e), "erro desconhecido");
    }
}

}  // namespace
