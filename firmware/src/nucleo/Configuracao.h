#pragma once
#include <cstddef>
#include <cstdint>

#include "log/Logger.h"

namespace coruja {

/// Limites de tamanho. Fixos e sem alocação dinâmica: a configuração vive em
/// `.bss` como a base de radares, e pelo mesmo motivo — não há heap no caminho
/// crítico deste firmware.
constexpr std::size_t kMaxRedes = 5;
/// IEEE 802.11 limita a SSID a 32 bytes.
constexpr std::size_t kMaxSsid = 32;
/// WPA2 aceita passphrase de 8 a 63 caracteres.
constexpr std::size_t kMaxSenha = 63;
constexpr std::size_t kMaxUrl = 160;
constexpr std::size_t kMaxNome = 23;
/// Segredo combinado com o servidor de recepção. 63 pelo mesmo motivo da
/// senha de Wi-Fi: é o tamanho de uma passphrase que uma pessoa consegue
/// digitar, e nada aqui ganha com mais.
constexpr std::size_t kMaxToken = 63;

constexpr std::uint8_t kVolumeMinimo = 50;
constexpr std::uint8_t kVolumeMaximo = 100;

/// Como o modo noturno decide.
///
/// `Automatico` usa nascer e pôr do sol calculados pela posição (R-61). Os
/// dois forçados existem para o que o cálculo **não** vê: garagem coberta ao
/// meio-dia, ou um trecho em que se queira a tela clara à noite.
enum class ModoNoturno : std::uint8_t { Automatico, SempreDia, SempreNoite };

const char* descreve(ModoNoturno m);

/// Uma rede Wi-Fi candidata para a atualização OTA.
struct Rede {
    char ssid[kMaxSsid + 1] = {};
    char senha[kMaxSenha + 1] = {};

    bool vazia() const { return ssid[0] == '\0'; }
    /// Rede aberta, sem senha. Permitido: existem pontos de acesso abertos, e
    /// recusá-los seria decidir pelo dono do aparelho.
    bool aberta() const { return senha[0] == '\0'; }
};

/// Configuração de instalação, lida do cartão em tempo de execução.
///
/// **Só entra aqui o que varia por instalação.** Preferências de operação —
/// brilho, fuso, tolerâncias de velocidade, raio de alerta — ficam no código,
/// por decisão do autor em 2026-09-20. O critério: se mudar o valor muda o
/// comportamento de segurança, é especificação e não configuração. Alguém que
/// pudesse zerar o desconto do `V_infra` teria um aparelho que avisa tarde
/// demais, e não saberia disso.
///
/// Ver `docs/adr/0002`.
struct Configuracao {
    /// Redes **em ordem de prioridade**. A varredura escolhe a primeira desta
    /// lista que estiver visível, e não a de sinal mais forte: a ordem é
    /// explícita, previsível, e o log consegue dizer por que escolheu.
    Rede        redes[kMaxRedes] = {};
    std::size_t n_redes = 0;

    /// Onde consultar a versão disponível. A resposta é **uma linha de texto
    /// qualquer** — data, número, hash — que o firmware guarda ao lado do
    /// `radares.bin` e compara como texto. Não impõe formato ao servidor.
    char url_versao[kMaxUrl + 1] = {};
    /// Onde baixar o `radares.bin`. O RF05.2 exige HTTPS.
    char url_base[kMaxUrl + 1] = {};

    /// Para onde mandar log, viagens e infrações. **Vazia desliga o recurso**
    /// — e essa é a configuração padrão, de propósito: o aparelho não manda
    /// nada para lugar nenhum enquanto alguém não disser para onde.
    ///
    /// Termina em `/`; o nome do arquivo é acrescentado a ela. O servidor de
    /// referência atende em `/envio/`.
    char url_envio[kMaxUrl + 1] = {};

    /// Segredo que **identifica esta unidade** para o servidor.
    ///
    /// O servidor guarda uma lista `nome=token` e usa o segredo para saber de
    /// qual aparelho veio o arquivo — é o que faz os registros de dois carros
    /// não se misturarem. Vai em todas as requisições, não só nos envios.
    ///
    /// Como ele chega ao servidor muda com a rota, e a razão é do lwIP:
    ///
    /// - **envio** (`ClienteEnvio`, TCP cru): cabeçalho `X-Coruja-Token`.
    /// - **download** (`ClienteHttp`, `http_client` do lwIP): parâmetro `?t=`
    ///   na URL, porque a `httpc_connection_t` não tem campo para cabeçalho
    ///   próprio e a biblioteca monta a requisição sem oferecer gancho.
    ///
    /// ⚠️ **Nunca escrever este valor no log.** Mesma regra da senha de
    /// Wi-Fi, e pelo mesmo motivo: o log vai para o cartão e o cartão sai do
    /// carro.
    ///
    /// ⚠️ Ele viaja **em claro**, porque este firmware não fala TLS. Não é
    /// autenticação forte e não protege o conteúdo de quem escuta a rede; ele
    /// existe para que a URL do servidor não seja um depósito aberto para
    /// quem a descobrir. Decisão consciente do autor em 2026-10-06.
    ///
    /// ⚠️ Na rota de download ele vai **na URL**, e token em URL aparece em
    /// log de acesso e de proxy — ao contrário de token em cabeçalho. Aqui
    /// pesa menos do que pareceria, porque tudo trafega em claro de qualquer
    /// forma; mas quem puser um proxy reverso na frente deve saber disso.
    char token_aparelho[kMaxToken + 1] = {};

    /// Servidor de hora. Vazio usa `kServidorNtpPadrao` (`pool.ntp.br`).
    ///
    /// Existe porque o relógio decide se um certificado está no prazo, e uma
    /// rede que bloqueie o pool público deixaria o aparelho sem hora e sem
    /// TLS. Quem tem servidor de hora na própria rede aponta para ele.
    char servidor_ntp[kMaxUrl + 1] = {};

    /// Escrever todas as mensagens de log também no cartão.
    ///
    /// Desligado por padrão, e a razão não é economia de código: gravar log a
    /// cada mensagem gasta escrita de cartão, e cartão tem número finito de
    /// ciclos. É recurso de diagnóstico, para ficar ligado enquanto se procura
    /// um defeito e desligado depois.
    bool log_para_cartao = false;

    /// Abaixo deste nível as mensagens são descartadas, no console e no
    /// cartão. `Info` por padrão — `Debug` inclui a varredura de Wi-Fi inteira
    /// e cada volume sondado, que é muito para uso normal.
    Nivel nivel_log = Nivel::Info;

    // ---- ajustes que o aparelho GRAVA de volta (menu do encoder) -------
    //
    // Ao contrário dos de cima, estes o firmware escreve. Ficam no mesmo
    // arquivo por decisão do autor, e a gravação é cirúrgica: troca o valor
    // das chaves conhecidas e preserva comentários e o resto byte a byte.

    /// Identifica a unidade. Com mais de um aparelho os ajustes divergem —
    /// ruído de cabine muda o volume, posição do painel muda o brilho — e
    /// trocar os cartões sem perceber aplicaria os ajustes do outro carro
    /// **em silêncio**. O nome aparece no boot e em cada linha do log.
    char nome[kMaxNome + 1] = {};

    /// Brilho da tela por período, em passos de 5, de 5 a 100 (R-61).
    std::uint8_t brilho_dia = 100;
    std::uint8_t brilho_noite = 20;

    ModoNoturno modo_noturno = ModoNoturno::Automatico;

    /// Volume do buzzer, de 50 a 100. **Nunca abaixo de 50**: janela aberta
    /// pede 100, ar-condicionado pede 50, e silenciar transformaria o
    /// aparelho em enfeite.
    std::uint8_t volume_buzzer = kVolumeMaximo;

    bool tem_rede() const { return n_redes > 0; }
    bool tem_urls() const {
        return url_versao[0] != '\0' && url_base[0] != '\0';
    }
    /// Verdadeiro quando há o mínimo para tentar uma atualização OTA. Sem
    /// isso o firmware opera normalmente — só não atualiza (RF07).
    bool ota_possivel() const { return tem_rede() && tem_urls(); }

    /// Verdadeiro quando há para onde mandar os arquivos e com que segredo.
    ///
    /// Exige o token junto da URL: um servidor que recebe sem segredo aceita
    /// de qualquer um, e configurar só a URL seria pedir exatamente isso sem
    /// perceber.
    ///
    /// **O item do menu aparece mesmo assim**, e a tela responde "SEM
    /// CONFIGURACAO". Esconder o item deixaria quem configurou errado sem
    /// nenhum sinal de que configurou errado — ele procuraria o item que não
    /// está lá e concluiria que o firmware não tem o recurso.
    bool envio_possivel() const {
        return tem_rede() && url_envio[0] != '\0' && token_aparelho[0] != '\0';
    }
};

}  // namespace coruja
