#include "nucleo/SincronizadorHora.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "apoio/LoggerMock.h"

namespace {

using namespace coruja;

class RelogioFalso : public RelogioPersistente {
public:
    std::int64_t valor = 0;   ///< zero = boot sem hora, como o RP2350 acorda
    int          ajustes = 0;

    std::int64_t agora_utc() const override { return valor; }
    void define_utc(std::int64_t s) override { valor = s; ++ajustes; }
};

class NtpFalso : public FonteNtp {
public:
    bool                     responde = true;
    std::int64_t             devolve = 0;
    std::vector<std::string> consultados;

    bool consulta(const char* servidor, std::int64_t* s, Logger&) override {
        consultados.emplace_back(servidor == nullptr ? "" : servidor);
        if (!responde) { return false; }
        *s = devolve;
        return true;
    }
};

Telemetria gps_com_data(int ano = 2026, int mes = 10, int dia = 6,
                        int h = 14, int m = 30, int s = 0) {
    Telemetria t;
    t.data_valida = true;
    t.ano = static_cast<std::uint16_t>(ano);
    t.mes = static_cast<std::uint8_t>(mes);
    t.dia = static_cast<std::uint8_t>(dia);
    t.hora = static_cast<std::uint8_t>(h);
    t.minuto = static_cast<std::uint8_t>(m);
    t.segundo = static_cast<std::uint8_t>(s);
    return t;
}

struct Bancada {
    RelogioFalso      relogio;
    NtpFalso          ntp;
    teste::LoggerMock log;

    SincronizadorHora cria() { return SincronizadorHora{relogio, ntp}; }
};

const std::int64_t kHoraNtp = segundos_de(TempoUtc{2026, 10, 6, 12, 0, 0});
const std::int64_t kHoraGps = segundos_de(TempoUtc{2026, 10, 6, 14, 30, 0});

// ============================================== a ordem: NTP antes do GPS

TEST(SincronizadorHora, o_ntp_vem_primeiro_mesmo_com_gps_disponivel) {
    // A razão está no caso de uso: OTA e envio só acontecem com Wi-Fi, ou
    // seja, com o carro parado onde há rede -- garagem, estacionamento
    // coberto. Tentar o GPS antes significaria esperar um fix falhar em
    // todas as vezes que o recurso é usado.
    Bancada b;
    b.ntp.devolve = kHoraNtp;
    auto s = b.cria();

    EXPECT_EQ(s.sincroniza(nullptr, gps_com_data(), b.log), OrigemHora::Ntp);
    EXPECT_EQ(b.relogio.valor, kHoraNtp);
    EXPECT_NE(b.relogio.valor, kHoraGps) << "o GPS nao foi consultado";
}

TEST(SincronizadorHora, sem_ntp_cai_para_o_gps) {
    // Rede com NTP bloqueado, ou pool fora do ar. O GPS cobre.
    Bancada b;
    b.ntp.responde = false;
    auto s = b.cria();

    EXPECT_EQ(s.sincroniza(nullptr, gps_com_data(), b.log), OrigemHora::Gps);
    EXPECT_EQ(b.relogio.valor, kHoraGps);
}

TEST(SincronizadorHora, sem_ntp_e_sem_fix_nao_inventa_hora) {
    // A garagem subterrânea sem NTP. Um relógio chutado seria pior que
    // nenhum: com TLS, data errada aceita certificado vencido.
    Bancada b;
    b.ntp.responde = false;
    auto s = b.cria();

    EXPECT_EQ(s.sincroniza(nullptr, Telemetria{}, b.log), OrigemHora::Nenhuma);
    EXPECT_EQ(b.relogio.ajustes, 0);
    EXPECT_TRUE(b.log.contem(Nivel::Error, "sem hora"));
}

TEST(SincronizadorHora, o_gps_nao_e_esperado_quando_nao_tem_data) {
    // Telemetria sem `data_valida` é simplesmente ignorada. Ficar aqui
    // esperando o receptor pegar sinal reintroduziria a espera que a ordem
    // existe para evitar -- e numa garagem ela não termina.
    Bancada b;
    b.ntp.responde = false;
    Telemetria sem_data = gps_com_data();
    sem_data.data_valida = false;
    auto s = b.cria();

    EXPECT_EQ(s.sincroniza(nullptr, sem_data, b.log), OrigemHora::Nenhuma);
}

// ========================================================== o que recusa

TEST(SincronizadorHora, ntp_com_hora_implausivel_nao_e_aceito) {
    Bancada b;
    b.ntp.devolve = 0;  // 1970
    auto s = b.cria();

    EXPECT_EQ(s.sincroniza(nullptr, gps_com_data(), b.log), OrigemHora::Gps)
        << "recusa o NTP ruim e segue para o GPS";
    EXPECT_EQ(b.relogio.valor, kHoraGps);
    EXPECT_TRUE(b.log.contem(Nivel::Warning, "implausivel"));
}

TEST(SincronizadorHora, gps_com_data_absurda_nao_e_aceito) {
    Bancada b;
    b.ntp.responde = false;
    auto s = b.cria();

    EXPECT_EQ(s.sincroniza(nullptr, gps_com_data(1999, 1, 1), b.log),
              OrigemHora::Nenhuma);
    EXPECT_EQ(b.relogio.ajustes, 0);
}

// ================================================= não reconsulta à toa

TEST(SincronizadorHora, ja_ajustado_nao_consulta_de_novo) {
    // Reconsultar a cada OTA gastaria rede e, pior, daria a quem controla a
    // rede uma segunda chance de mover um relógio que já estava bom.
    Bancada b;
    b.ntp.devolve = kHoraNtp;
    auto s = b.cria();

    ASSERT_EQ(s.sincroniza(nullptr, Telemetria{}, b.log), OrigemHora::Ntp);
    EXPECT_EQ(s.sincroniza(nullptr, Telemetria{}, b.log),
              OrigemHora::JaAjustado);
    EXPECT_EQ(b.ntp.consultados.size(), 1U);
    EXPECT_EQ(b.relogio.ajustes, 1);
}

TEST(SincronizadorHora, relogio_que_voltou_a_zero_e_ressincronizado) {
    // Afirma o EFEITO, não a bandeira: se o relógio perdeu a hora, a
    // marca interna de "já sincronizado" não pode mascarar isso.
    Bancada b;
    b.ntp.devolve = kHoraNtp;
    auto s = b.cria();
    ASSERT_EQ(s.sincroniza(nullptr, Telemetria{}, b.log), OrigemHora::Ntp);

    b.relogio.valor = 0;
    EXPECT_EQ(s.sincroniza(nullptr, Telemetria{}, b.log), OrigemHora::Ntp);
    EXPECT_EQ(b.ntp.consultados.size(), 2U);
}

TEST(SincronizadorHora, esquecer_forca_nova_consulta) {
    Bancada b;
    b.ntp.devolve = kHoraNtp;
    auto s = b.cria();
    ASSERT_EQ(s.sincroniza(nullptr, Telemetria{}, b.log), OrigemHora::Ntp);

    s.esquece();
    EXPECT_EQ(s.sincroniza(nullptr, Telemetria{}, b.log), OrigemHora::Ntp);
    EXPECT_EQ(b.ntp.consultados.size(), 2U);
}

// ============================================================== servidor

TEST(SincronizadorHora, sem_servidor_configurado_usa_o_pool_brasileiro) {
    Bancada b;
    b.ntp.devolve = kHoraNtp;
    auto s = b.cria();

    s.sincroniza(nullptr, Telemetria{}, b.log);
    ASSERT_EQ(b.ntp.consultados.size(), 1U);
    EXPECT_EQ(b.ntp.consultados[0], kServidorNtpPadrao);
    EXPECT_EQ(std::string(kServidorNtpPadrao), "pool.ntp.br");
}

TEST(SincronizadorHora, servidor_vazio_tambem_cai_no_padrao) {
    // Uma chave presente e em branco no coruja.cfg não pode virar uma
    // consulta a host nenhum.
    Bancada b;
    b.ntp.devolve = kHoraNtp;
    auto s = b.cria();

    s.sincroniza("", Telemetria{}, b.log);
    EXPECT_EQ(b.ntp.consultados[0], kServidorNtpPadrao);
}

TEST(SincronizadorHora, servidor_configurado_e_respeitado) {
    Bancada b;
    b.ntp.devolve = kHoraNtp;
    auto s = b.cria();

    s.sincroniza("a.ntp.br", Telemetria{}, b.log);
    EXPECT_EQ(b.ntp.consultados[0], "a.ntp.br");
}

TEST(SincronizadorHora, toda_origem_tem_nome_proprio) {
    for (auto o : {OrigemHora::Nenhuma, OrigemHora::JaAjustado,
                   OrigemHora::Ntp, OrigemHora::Gps}) {
        EXPECT_STRNE(descreve(o), "desconhecida");
    }
}

}  // namespace
