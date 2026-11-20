#include "rede/AtualizadorOta.h"

#include <cstdio>
#include <cstring>

#include "log/Logger.h"
#include "nucleo/Texto.h"
#include "nucleo/Url.h"

namespace coruja {
namespace {

/// Junta a resposta da consulta de versão num buffer. É uma linha curta, então
/// aqui a acumulação cabe — ao contrário do `radares.bin`.
struct ColetaVersao {
    char*       destino;
    std::size_t capacidade;
    std::size_t usados = 0;
    bool        estourou = false;
};

bool ao_receber_versao(void* contexto, const std::uint8_t* bytes,
                       std::size_t tamanho) {
    auto* coleta = static_cast<ColetaVersao*>(contexto);
    for (std::size_t i = 0; i < tamanho; ++i) {
        if (coleta->usados >= coleta->capacidade) {
            coleta->estourou = true;
            return true;
        }
        coleta->destino[coleta->usados++] = static_cast<char>(bytes[i]);
    }
    return true;
}

/// O download tem dois destinos ao mesmo tempo: o verificador e o cartão. Não
/// há etapa intermediária em RAM — 214 KB não cabem ao lado dos 281 KB que a
/// base reserva.
struct DestinoDownload {
    VerificadorDownload* verificador = nullptr;
    Armazenamento*       cartao = nullptr;
    ObservadorOta*       observador = nullptr;
    CaoDeGuarda*         cao = nullptr;
    bool                 falhou_a_escrita = false;
    bool                 passou_do_teto = false;
};

/// O tamanho que o cabecalho promete, ou zero quando ele promete mais do que
/// a placa comporta.
///
/// A conta e feita so depois de conferir o `n_pontos` contra a capacidade:
/// com o numero que o servidor quiser, `n_pontos * kTamRegistro` passa de
/// 32 bits, e no Pico o `size_t` tem 32. A tela recebia um total que tinha
/// dado a volta. Zero e o "ainda nao se sabe" que quem desenha ja trata.
std::size_t total_prometido(const CabecalhoBase& c) {
    if (c.n_pontos > kCapacidadeFirmware) { return 0; }
    return c.tamanho() + std::size_t{c.n_pontos} * kTamRegistro;
}

bool ao_receber_base(void* contexto, const std::uint8_t* bytes,
                     std::size_t tamanho) {
    auto* destino = static_cast<DestinoDownload*>(contexto);
    // Parado e parado, mesmo que o transporte insista.
    if (destino->falhou_a_escrita || destino->passou_do_teto) { return false; }

    // O teto ANTES do cartao: nada alem do que a placa comporta e gravado.
    // `bytes_recebidos` nunca passa de `kTetoBase`, porque o pedaco que o
    // faria passar nao entra -- entao a subtracao nao da a volta.
    auto& v = *destino->verificador;
    if (tamanho > kTetoBase - v.bytes_recebidos()) {
        destino->passou_do_teto = true;
        return false;
    }
    v.alimenta(bytes, tamanho);

    // Depois da primeira falha de escrita, PARA O DOWNLOAD: continuar era ate
    // 120 s de aparelho bloqueado, sem GPS e sem alerta, para um arquivo que
    // ja nao seria gravado.
    if (!destino->cartao->escreve(bytes, tamanho)) {
        destino->falhou_a_escrita = true;
        return false;
    }

    // **O progresso do download, e e so este.** Bytes que chegaram E foram
    // para o cartao. Um pedido que gasta os 120 s do teto sem entregar nada
    // nao passa por aqui uma vez sequer, e e exatamente assim que o cao
    // morde um transporte travado.
    if (destino->cao != nullptr) { destino->cao->alimenta(); }

    if (destino->observador != nullptr) {
        // O total so existe depois do cabecalho: e ele que diz quantos
        // pontos vem. Antes disso vai zero, e quem desenha trata.
        //
        // Avisa a CADA pedaco, sem limitar a taxa aqui: quem desenha ja
        // redesenha so quando o pixel da barra muda, e duplicar essa
        // decisao seria dois lugares para ajustar o mesmo compromisso.
        destino->observador->progresso(
            v.bytes_recebidos(),
            v.tem_cabecalho() ? total_prometido(v.cabecalho()) : 0);
    }
    return true;
}

/// O resultado de um pedido que falhou, pelo que a falha diz.
///
/// `passageira` e o resultado da falha que pode sumir tentando de novo --
/// `FalhaAoConsultar` na consulta, `FalhaAoBaixar` no download --, e e o
/// unico que o laco de tentativas repete. A tela precisa da diferenca:
/// DOWNLOAD INCOMPLETO pede outra tentativa; SERVIDOR RECUSOU pede o
/// `coruja.cfg`, e CERTIFICADO RECUSADO, o relogio.
ResultadoOta resultado_da_falha(const ResultadoHttp& r,
                                ResultadoOta passageira) {
    switch (classifica_falha(r)) {
        case FalhaHttp::SemResposta:
        case FalhaHttp::ErroNoServidor: return passageira;
        case FalhaHttp::Url:            return ResultadoOta::SemConfiguracao;
        case FalhaHttp::Certificado:    return ResultadoOta::CertificadoRecusado;
        case FalhaHttp::GrandeDemais:
        case FalhaHttp::Recusado:       return ResultadoOta::ServidorRecusou;
    }
    return passageira;  // fora de faixa: o lado que insiste
}

/// O que chegou no download, em uma linha -- e o despejo inteiro em DEBUG.
///
/// 🔴 **Eram catorze linhas por atualizacao** (27/10/2026), com moldura de
/// tracinhos, repetindo campo a campo um cabecalho que so interessa quando
/// algo DIVERGE. O diario e lido por rede; a bancada e que precisa da tabela.
void imprime_cabecalho(const VerificadorDownload& v, Logger& log) {
    char msg[110];
    const CabecalhoBase& c = v.cabecalho();

    log.debug("base", "---- cabecalho do radares.bin recebido ----");

    char magic[5] = {};
    std::memcpy(magic, &c.magic, 4);
    for (int i = 0; i < 4; ++i) {
        if (magic[i] < 32 || magic[i] > 126) {
            magic[i] = '?';
        }
    }
    std::snprintf(msg, sizeof msg, "  magic        : '%s' (0x%08lX)%s", magic,
                  static_cast<unsigned long>(c.magic),
                  c.magic == kMagic ? "" : "  <- ESPERADO 'RDR1'");
    log.debug("base", msg);

    std::snprintf(msg, sizeof msg, "  versao       : %u%s",
                  static_cast<unsigned>(c.versao),
                  c.versao == kVersao ? "" : "  <- INCOMPATIVEL");
    log.debug("base", msg);
    std::snprintf(msg, sizeof msg,
                  "  exp_escala   : %u  (coordenadas x10^%u)",
                  static_cast<unsigned>(c.exp_escala),
                  static_cast<unsigned>(c.exp_escala));
    log.debug("base", msg);
    std::snprintf(msg, sizeof msg, "  tam_registro : %u B",
                  static_cast<unsigned>(c.tam_registro));
    log.debug("base", msg);
    std::snprintf(msg, sizeof msg, "  n_pontos     : %lu  (declarados)",
                  static_cast<unsigned long>(c.n_pontos));
    log.debug("base", msg);
    std::snprintf(msg, sizeof msg, "  crc32        : 0x%08lX  (do cabecalho)",
                  static_cast<unsigned long>(c.crc));
    log.debug("base", msg);

    log.debug("base", "---- e o que de fato chegou ----");
    std::snprintf(msg, sizeof msg, "  bytes        : %lu  (%lu de dados)",
                  static_cast<unsigned long>(v.bytes_recebidos()),
                  static_cast<unsigned long>(v.bytes_de_dados()));
    log.debug("base", msg);
    std::snprintf(msg, sizeof msg, "  pontos       : %lu%s",
                  static_cast<unsigned long>(v.pontos_recebidos()),
                  v.pontos_recebidos() == c.n_pontos ? "  (confere)"
                                                     : "  <- DIVERGE");
    log.debug("base", msg);
    std::snprintf(msg, sizeof msg, "  crc32        : 0x%08lX%s",
                  static_cast<unsigned long>(v.crc_calculado()),
                  v.crc_calculado() == c.crc ? "  (confere)" : "  <- DIVERGE");
    log.debug("base", msg);
    log.debug("base", "------------------------------------------");

    // A linha do campo: os numeros que chegaram e se eles batem com o que o
    // cabecalho prometeu. Divergencia aqui e a unica coisa que o despejo
    // acima diria de novo.
    const bool confere = v.pontos_recebidos() == c.n_pontos &&
                         v.crc_calculado() == c.crc && c.magic == kMagic &&
                         c.versao == kVersao;
    std::snprintf(msg, sizeof msg,
                  "recebido: %lu pontos, %lu B, crc 0x%08lX - %s",
                  static_cast<unsigned long>(v.pontos_recebidos()),
                  static_cast<unsigned long>(v.bytes_recebidos()),
                  static_cast<unsigned long>(v.crc_calculado()),
                  confere ? "confere com o cabecalho"
                          : "DIVERGE do cabecalho (detalhe em debug)");
    if (confere) { log.info("base", msg); }
    else         { log.error("base", msg); }
}

}  // namespace

const char* descreve(FaseOta fase) {
    // Texto curto de proposito: vai para a faixa de 26 caracteres da tela,
    // e uma frase mais longa rolaria em vez de ser lida de relance.
    switch (fase) {
        case FaseOta::Conectando:  return "CONECTANDO";
        case FaseOta::Consultando: return "CONSULTANDO VERSAO";
        case FaseOta::JaEmDia:     return "JA ESTA EM DIA";
        case FaseOta::Baixando:    return "BAIXANDO";
        case FaseOta::Verificando: return "VERIFICANDO";
        case FaseOta::Gravando:    return "GRAVANDO";
        case FaseOta::Concluida:   return "ATUALIZADA";
        case FaseOta::Falhou:      return "FALHOU";
    }
    return "?";
}

const char* descreve(ResultadoOta resultado) {
    switch (resultado) {
        case ResultadoOta::Atualizada:       return "base atualizada";
        case ResultadoOta::JaEstavaEmDia:    return "ja estava na ultima versao";
        case ResultadoOta::SemConfiguracao:  return "sem rede ou sem URLs configuradas";
        case ResultadoOta::FalhaDeRede:      return "nao conseguiu conectar";
        case ResultadoOta::FalhaAoConsultar: return "nao conseguiu consultar a versao";
        case ResultadoOta::FalhaAoBaixar:    return "o download nao completou";
        case ResultadoOta::BaseRecusada:     return "base recusada na verificacao";
        case ResultadoOta::FalhaAoGravar:    return "chegou integra, mas o cartao nao aceitou";
        case ResultadoOta::ServidorRecusou:
            return "o servidor recusou o pedido (token? URL?)";
        case ResultadoOta::CertificadoRecusado:
            return "o certificado do servidor foi recusado (relogio acertado?)";
    }
    return "resultado desconhecido";
}

const char* descreve_curto(ResultadoOta resultado) {
    // Maiusculas e sem acento, como o resto da tela: a fonte de texto do
    // §4.1 e um subconjunto gerado, e acento nao esta nele.
    switch (resultado) {
        case ResultadoOta::Atualizada:       return "ATUALIZADA";
        case ResultadoOta::JaEstavaEmDia:    return "JA ESTA EM DIA";
        case ResultadoOta::SemConfiguracao:  return "SEM CONFIGURACAO";
        case ResultadoOta::FalhaDeRede:      return "SEM WI-FI";
        case ResultadoOta::FalhaAoConsultar: return "SERVIDOR NAO RESPONDE";
        case ResultadoOta::FalhaAoBaixar:    return "DOWNLOAD INCOMPLETO";
        case ResultadoOta::BaseRecusada:     return "ARQUIVO CORROMPIDO";
        case ResultadoOta::FalhaAoGravar:    return "CARTAO NAO GRAVOU";
        case ResultadoOta::ServidorRecusou:  return "SERVIDOR RECUSOU";
        case ResultadoOta::CertificadoRecusado: return "CERTIFICADO RECUSADO";
    }
    return "ERRO DESCONHECIDO";
}


void AtualizadorOta::avisa(FaseOta fase, unsigned tentativa) {
    if (observador_ != nullptr) {
        observador_->fase(fase, tentativa);
    }
}

ResultadoOta AtualizadorOta::executa(const Configuracao& cfg, Logger& log) {
    if (!cfg.ota_possivel()) {
        log.warning("ota", "sem rede ou sem URLs: nada a fazer");
        return ResultadoOta::SemConfiguracao;
    }

    Url url_versao;
    Url url_base;
    if (analisa_url(cfg.url_versao, &url_versao) != ErroUrl::Nenhum ||
        analisa_url(cfg.url_base, &url_base) != ErroUrl::Nenhum) {
        log.error("ota", "URL da configuracao nao e analisavel");
        return ResultadoOta::SemConfiguracao;
    }

    // O segredo do aparelho NAO entra na URL: ele vai no cabecalho
    // `X-Coruja-Token`, que o `ClienteTls` acrescenta a toda requisicao.
    //
    // Houve uma versao em que ele vinha aqui, como `?t=`, porque o
    // `http_client` do lwIP montava o pedido internamente e nao tinha onde
    // encaixar cabecalho proprio. Com TLS esse transporte foi reescrito, o
    // gancho passou a existir, e o segredo saiu da URL -- onde entrava no log
    // de acesso do servidor e de qualquer proxy no caminho.

    log.debug("ota", "==== atualizacao solicitada ====");
    le_versao_local(log);

    avisa(FaseOta::Conectando);
    // ⚠️ **O cao sai solto para a associacao, e volta logo depois.** A
    // varredura leva ate 12 s e a associacao ate 20 s, e as duas sao
    // chamadas do SDK sem gancho por dentro: 20 s passa do teto de 16,777 s
    // do hardware, entao manter o cao armado reiniciaria o aparelho em TODA
    // conexao que falha -- senha errada, rede fora de alcance. Isso e
    // desfecho comum, nao travamento, e o preco de reiniciar e perder a
    // viagem em andamento.
    //
    // E o unico buraco da vigilancia, e ele e consciente: um cyw43 que
    // travar na associacao continua deixando o aparelho mudo.
    if (cao_ != nullptr) { cao_->suspende(); }
    const auto erro_rede = rede_.conecta(cfg, log);
    if (cao_ != nullptr) { cao_->retoma(); }
    if (erro_rede != ErroWifi::Nenhum) {
        char msg[64];
        std::snprintf(msg, sizeof msg, "rede: %s", descreve(erro_rede));
        log.error("ota", msg);
        avisa(FaseOta::Falhou);
        return ResultadoOta::FalhaDeRede;
    }

    // AGORA, e nao antes: o `conecta()` acima e quem chama o
    // `cyw43_arch_init()`. Qualquer uso da pilha de rede antes deste ponto
    // fala com um radio que ainda nao existe -- e o aparelho trava por
    // completo, sem erro. Ver `PreparoDeSessao`.
    if (preparo_ != nullptr) {
        preparo_->apos_conectar(log);
    }

    // `desconecta` tem de acontecer em TODA saída daqui para baixo. Por isso
    // o que fala com o servidor devolve o resultado em vez de encerrar: um
    // retorno adiantado lá dentro não tem como esquecer o rádio associado.
    return encerra(fala_com_o_servidor(url_versao, url_base, log), log);
}

void AtualizadorOta::le_versao_local(Logger& log) {
    // A versão local vem do CARTÃO. Guardá-la só em RAM fazia o primeiro
    // clique depois de cada boot rebaixar 214 KB sem necessidade.
    std::size_t lidos = 0;
    versao_local_[0] = '\0';
    const auto leitura_versao = cartao_.le_arquivo(kArquivoVersao, versao_local_,
                                                   kMaxVersao, &lidos, log);
    if (leitura_versao == ErroCartao::Nenhum) {
        apara_branco(versao_local_, &lidos);
    } else if (leitura_versao == ErroCartao::ArquivoAusente) {
        // Normal na primeira atualização. Não é erro, e não deve soar como um.
        log.debug("ota", "sem versao registrada no cartao: primeira atualizacao");
    } else {
        char msg[96];
        std::snprintf(msg, sizeof msg, "versao local ilegivel (%s): vai rebaixar",
                      descreve(leitura_versao));
        log.warning("ota", msg);
    }
}

ResultadoOta AtualizadorOta::encerra(ResultadoOta r, Logger& log) {
    // A fase final sai daqui, e nao de cada ponto de retorno: e o unico
    // lugar por onde todos passam, e foi escrito justamente para que o
    // `desconecta` nao fosse esquecido em nenhum deles.
    avisa(r == ResultadoOta::Atualizada      ? FaseOta::Concluida
          : r == ResultadoOta::JaEstavaEmDia ? FaseOta::JaEmDia
                                             : FaseOta::Falhou);
    char msg[96];
    std::snprintf(msg, sizeof msg, "resultado: %s", descreve(r));
    log.info("ota", msg);
    rede_.desconecta(log);
    // A moldura some: a linha `resultado:` acima e quem fecha o evento.
    log.debug("ota", "==== fim da atualizacao ====");
    return r;
}

ResultadoOta AtualizadorOta::fala_com_o_servidor(const Url& url_versao,
                                                 const Url& url_base,
                                                 Logger& log) {
    // ---- 1. consultar a versão disponível --------------------------------
    ResultadoOta motivo = ResultadoOta::FalhaAoConsultar;
    if (!consulta_versao(url_versao, log, &motivo)) {
        return motivo;
    }
    if (ja_em_dia(log)) {
        return ResultadoOta::JaEstavaEmDia;
    }

    // ---- 2. baixar, gravando e verificando ao mesmo tempo ----------------
    log.debug("ota", "versao diferente: baixando a base");
    return baixa_com_tentativas(url_base, log);
}

bool AtualizadorOta::consulta_versao(const Url& url, Logger& log,
                                     ResultadoOta* motivo) {
    avisa(FaseOta::Consultando);
    ColetaVersao coleta{versao_remota_, kMaxVersao};
    // Nas duas pontas do pedido, e nao por dentro dele: a consulta e uma
    // linha curta, e o que ela tem de longo e a espera por ela.
    alimenta_o_cao();
    const auto consulta = http_.baixa(url, ao_receber_versao, &coleta, log,
                                      15'000);
    alimenta_o_cao();
    if (!consulta.ok()) {
        // O status vai junto: um 401 e um 503 pedem consertos diferentes.
        char msg[96];
        std::snprintf(msg, sizeof msg, "consulta de versao: %s (status %u)",
                      descreve(consulta.erro),
                      static_cast<unsigned>(consulta.status));
        log.error("ota", msg);
        *motivo = resultado_da_falha(consulta, ResultadoOta::FalhaAoConsultar);
        return false;
    }
    if (coleta.estourou) {
        log.warning("ota", "resposta de versao maior que o buffer; truncada");
    }
    versao_remota_tam_ = coleta.usados;
    apara_branco(versao_remota_, &versao_remota_tam_);
    if (versao_remota_tam_ == 0) {
        log.error("ota", "o servidor devolveu versao vazia");
        *motivo = ResultadoOta::FalhaAoConsultar;
        return false;
    }
    // Parseada UMA VEZ, aqui na entrada: dado de fora so vira versao depois
    // de passar. Ela e registrada no log -- que e lido linha a linha para
    // saber o que aconteceu, e uma quebra de linha nela escreveria linhas
    // que ninguem registrou -- e gravada no `versao.txt`, de onde volta na
    // comparacao do clique seguinte. A mesma regra do token e da URL.
    if (!seguro_para_cabecalho(versao_remota_)) {
        log.error("ota", "a versao do servidor tem caractere que nao se "
                         "registra; recusada");
        versao_remota_[0] = '\0';
        versao_remota_tam_ = 0;
        *motivo = ResultadoOta::FalhaAoConsultar;
        return false;
    }
    return true;
}

bool AtualizadorOta::ja_em_dia(Logger& log) const {
    // Uma linha para a comparacao inteira: duas metades de uma pergunta so
    // eram duas linhas por consulta, e a resposta esta na diferenca.
    char msg[224];
    std::snprintf(msg, sizeof msg, "versao: local '%s' x servidor '%s'",
                  versao_local_[0] != '\0' ? versao_local_ : "(nenhuma)",
                  versao_remota_);
    log.info("ota", msg);

    if (std::strcmp(versao_local_, versao_remota_) != 0) {
        return false;
    }
    if (vigente_em_uso_) {
        return true;
    }
    // A versao descreve o `radares.bin`, e nao e ele que esta em uso. Ver
    // `informa_vigente_em_uso`.
    log.warning("ota", "mesma versao, mas a base em uso nao e o "
                       "radares.bin: baixando de novo");
    return false;
}

ResultadoOta AtualizadorOta::baixa_com_tentativas(const Url& url,
                                                  Logger& log) {
    ResultadoOta ultimo_erro = ResultadoOta::FalhaAoBaixar;
    for (unsigned tentativa = 1; tentativa <= kTentativas; ++tentativa) {
        if (tentativa > 1) {
            char msg[64];
            std::snprintf(msg, sizeof msg, "tentativa %u de %u, apos %lu ms",
                          tentativa, kTentativas,
                          static_cast<unsigned long>(kEsperaEntreTentativasMs));
            log.warning("ota", msg);
            pausa_.espera_ms(kEsperaEntreTentativasMs);
            // Depois da espera, e nao antes: a tentativa seguinte comecou, e
            // esse e o progresso. Sem isto a espera de 5 s se somaria aos
            // 15 s de inatividade do download anterior, e os 20 s passariam
            // do prazo.
            alimenta_o_cao();
        }
        bool repete = false;
        ultimo_erro = tenta_baixar(url, tentativa, log, &repete);
        if (!repete) {
            return ultimo_erro;
        }
    }

    log.error("ota", "as tentativas se esgotaram; a base vigente segue intacta");
    return ultimo_erro;
}

ResultadoOta AtualizadorOta::tenta_baixar(const Url& url, unsigned tentativa,
                                          Logger& log, bool* repete) {
    char msg[96];
    // Quase toda falha daqui merece outra tentativa; as que nao merecem
    // dizem isso no ponto em que acontecem.
    *repete = true;

    avisa(FaseOta::Baixando, tentativa);
    verificador_.reinicia();
    const auto abertura = cartao_.abre_para_escrita(kArquivoTmp, log);
    if (abertura != ErroCartao::Nenhum) {
        std::snprintf(msg, sizeof msg, "nao deu para abrir '%s': %s",
                      kArquivoTmp, descreve(abertura));
        log.error("ota", msg);
        return ResultadoOta::FalhaAoGravar;
    }

    DestinoDownload destino;
    destino.verificador = &verificador_;
    destino.cartao = &cartao_;
    destino.observador = observador_;
    destino.cao = cao_;

    alimenta_o_cao();
    const auto download = http_.baixa(url, ao_receber_base, &destino, log,
                                      120'000);
    // O pedido voltou: isso e progresso, qualquer que tenha sido o desfecho.
    // Por dentro dele quem alimenta e `ao_receber_base`, so com pedaco
    // gravado.
    alimenta_o_cao();

    // Os motivos daqui ANTES do erro do cliente: quando o destino para o
    // download, o cliente volta com `Interrompida`, e a causa e esta.
    if (destino.falhou_a_escrita) {
        log.error("ota", "o cartao recusou a escrita: download interrompido");
        cartao_.descarta_escrita(kArquivoTmp, log);
        return ResultadoOta::FalhaAoGravar;
    }
    if (destino.passou_do_teto) {
        std::snprintf(msg, sizeof msg,
                      "base RECUSADA: passou de %lu B, mais do que a placa "
                      "comporta", static_cast<unsigned long>(kTetoBase));
        log.error("ota", msg);
        cartao_.descarta_escrita(kArquivoTmp, log);
        // O servidor mandaria o mesmo arquivo grande de novo.
        *repete = false;
        return ResultadoOta::BaseRecusada;
    }
    if (!download.ok()) {
        std::snprintf(msg, sizeof msg, "download: %s (status %u, %lu B chegaram)",
                      descreve(download.erro),
                      static_cast<unsigned>(download.status),
                      static_cast<unsigned long>(verificador_.bytes_recebidos()));
        log.error("ota", msg);
        cartao_.descarta_escrita(kArquivoTmp, log);
        // So o que pode mudar se repete. Um 401, um 404 ou um certificado
        // recusado voltariam iguais cinco segundos depois.
        const ResultadoOta r =
            resultado_da_falha(download, ResultadoOta::FalhaAoBaixar);
        *repete = r == ResultadoOta::FalhaAoBaixar;
        return r;
    }
    return verifica_e_promove(tentativa, log, repete);
}

ResultadoOta AtualizadorOta::verifica_e_promove(unsigned tentativa,
                                                Logger& log, bool* repete) {
    char msg[96];
    avisa(FaseOta::Verificando, tentativa);
    imprime_cabecalho(verificador_, log);

    // Passo 2 do RF05.2. Valida ANTES de mexer na base vigente: é o que
    // garante que um arquivo remoto corrompido não derrube a que funciona.
    const auto veredito = verificador_.conclui(kCapacidadeFirmware);
    if (veredito != ErroBase::Nenhum) {
        std::snprintf(msg, sizeof msg, "base RECUSADA: %s", descreve(veredito));
        log.error("ota", msg);
        cartao_.descarta_escrita(kArquivoTmp, log);
        return ResultadoOta::BaseRecusada;
    }

    if (cartao_.conclui_escrita(log) != ErroCartao::Nenhum) {
        cartao_.descarta_escrita(kArquivoTmp, log);
        return ResultadoOta::FalhaAoGravar;
    }
    // Fechar o arquivo descarrega a FAT, e a troca atomica renomeia tres
    // nomes. Sao etapas curtas e LIMITADAS -- cada uma acontece uma vez --,
    // entao concluir cada uma e progresso.
    alimenta_o_cao();

    // ---- 3 e 4. a troca atômica ------------------------------------------
    avisa(FaseOta::Gravando, tentativa);
    const auto troca = cartao_.promove(kArquivoTmp, kArquivoBase, kArquivoBak,
                                       log);
    if (troca != ErroCartao::Nenhum) {
        // Renomeacao que falha nao melhora na tentativa seguinte, e repetir
        // arriscaria o estado do cartao.
        std::snprintf(msg, sizeof msg, "troca atomica: %s", descreve(troca));
        log.error("ota", msg);
        *repete = false;
        return ResultadoOta::FalhaAoGravar;
    }
    alimenta_o_cao();
    // O que esta no lugar agora e o que acabou de passar na verificacao.
    vigente_em_uso_ = true;

    // ---- 5. a versão, por último -----------------------------------------
    //
    // Depois da base estar no lugar, de propósito. Se o aparelho desligar
    // entre as duas, sobra a versão antiga e o próximo clique rebaixa:
    // desperdício de rede, que é o modo de falha certo para escolher.
    std::memcpy(versao_local_, versao_remota_, versao_remota_tam_ + 1);
    const auto gravou_versao = cartao_.grava_arquivo(
        kArquivoVersao, versao_remota_, versao_remota_tam_, log);
    if (gravou_versao != ErroCartao::Nenhum) {
        log.warning("ota", "base gravada, mas a versao nao: vai rebaixar "
                           "no proximo clique");
    }

    std::snprintf(msg, sizeof msg, "base gravada em '%s' (%lu bytes)",
                  kArquivoBase,
                  static_cast<unsigned long>(verificador_.bytes_recebidos()));
    log.info("ota", msg);
    *repete = false;
    return ResultadoOta::Atualizada;
}

}  // namespace coruja
