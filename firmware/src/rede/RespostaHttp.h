#pragma once
#include <cstddef>
#include <cstdint>

namespace coruja {

/// Leitura da resposta crua do servidor de recepção.
///
/// **Vive separado do cliente de propósito.** O `ClienteEnvio` só compila para
/// o RP2350, porque arrasta o lwIP junto; estas duas funções são a parte da
/// conversa que decide — uma diz se o pedido deu certo, a outra produz o
/// número que autoriza apagar o arquivo do cartão. Enterradas no `.cpp` do
/// cliente, seriam o único trecho do caminho de envio sem teste nenhum.

/// Código da linha de status (`HTTP/1.1 201 Created` → 201).
///
/// Zero quando a resposta não começa com uma linha de status reconhecível —
/// que é o que se recebe de um servidor que não é o esperado, ou de uma
/// conexão que entregou lixo.
std::uint32_t status_da_resposta(const char* texto, std::size_t tamanho);

/// O corpo, depois da linha em branco que separa os cabeçalhos.
///
/// Devolve `nullptr` se os cabeçalhos ainda não terminaram. Aceita `\r\n\r\n`
/// e `\n\n`: o servidor de referência manda o primeiro, mas um proxy no
/// caminho pode normalizar, e recusar por causa disso seria falhar por uma
/// diferença que não muda nada.
const char* corpo_da_resposta(const char* texto, std::size_t tamanho,
                              std::size_t* tamanho_do_corpo);

/// Lê oito dígitos hexadecimais, o CRC-32 que o servidor devolve.
///
/// Exige **exatamente oito**, e é por isso que não usa `strtoul`: "1a2b" seria
/// aceito e viraria 0x1A2B, um número plausível e errado — e um CRC errado que
/// por acaso bate autoriza apagar o único exemplar de um arquivo.
///
/// Espaço em branco em volta é tolerado; qualquer outro caractere recusa.
bool le_crc_hex(const char* texto, std::size_t tamanho, std::uint32_t* destino);


/// Lê uma resposta HTTP que chega **aos pedaços**, pela rede.
///
/// As funções acima servem a uma resposta pequena, já inteira na mão. Esta
/// classe serve ao `radares.bin`: 214 KB que nunca existem inteiros em lugar
/// nenhum, entregues ao chamador à medida que chegam.
///
/// **Entende `Transfer-Encoding: chunked`**, e não é zelo: quem responde é o
/// Cloudflare, não o servidor de referência. Ele decide sozinho se manda com
/// `Content-Length` ou em pedaços, e a escolha muda com compressão, com
/// versão de protocolo e com o humor do dia. Um cliente que só entendesse
/// `Content-Length` gravaria os cabeçalhos de pedaço dentro do `radares.bin`
/// — e o CRC acusaria corrupção sem dizer por quê.
class LeitorRespostaHttp {
public:
    /// Chamada com os bytes do CORPO, já sem cabeçalhos nem marcação de
    /// pedaço. Pode ser chamada várias vezes por bloco recebido.
    ///
    /// `false` pede para parar: nenhum byte mais é entregue, e `alimenta`
    /// devolve `false` com `destino_parou()` verdadeiro — o que separa "quem
    /// recebe desistiu" de "a resposta veio malformada".
    using AoReceberCorpo = bool (*)(void* contexto, const std::uint8_t* bytes,
                                    std::size_t tamanho);

    /// Teto dos cabeçalhos. O Cloudflare manda bastante coisa (`cf-ray`,
    /// `alt-svc`, `server-timing`); 2 KiB cobre com folga, e quem passar
    /// disso é resposta que não interessa.
    static constexpr std::size_t kMaxCabecalhos = 2048;

    /// Teto de um corpo anunciado, em bytes. 1 GiB.
    ///
    /// Não é limite de produto — o `radares.bin` tem 214 KB. É o ponto em que
    /// `Content-Length` ou tamanho de pedaço deixam de ser número e passam a
    /// ser lixo: acumular além disso estoura o inteiro, e **estouro com sinal
    /// é comportamento indefinido**, que dá ao compilador licença para apagar
    /// a verificação feita depois da conta. A recusa vem antes da conta.
    static constexpr long kTetoCorpo = 1L << 30;

    void reinicia();

    /// Alimenta bytes crus da conexão. `false` em resposta malformada — e aí
    /// quem chama deve abortar, porque não há como saber o que seria corpo.
    bool alimenta(const std::uint8_t* bytes, std::size_t tamanho,
                  AoReceberCorpo ao_receber, void* contexto);

    bool cabecalhos_prontos() const { return fase_ != Fase::Cabecalhos; }
    std::uint32_t status() const { return status_; }

    /// O corpo terminou de chegar **e isso é sabido**: o `Content-Length` foi
    /// atingido, veio o pedaço de tamanho zero, ou — numa resposta sem
    /// nenhum dos dois — a conexão fechou (`fim_do_fluxo`).
    ///
    /// Falso depois do `fim_do_fluxo` é **corpo truncado**, e quem chama tem
    /// de tratá-lo como falha.
    bool completa() const { return fase_ == Fase::Fim; }

    /// A conexão fechou. Quem sabe disso é o transporte; o que isso quer
    /// dizer é daqui.
    ///
    /// Só a resposta que não anunciou o próprio fim termina no fechamento.
    /// Nas outras, fechar antes do `Content-Length` ou do pedaço zero deixa
    /// `completa()` falso. Até 17/11/2026 o `ClienteTls` tomava todo
    /// fechamento como fim do corpo, e um `radares.versao` cortado no meio,
    /// com 200, ia parar no `versao.txt`.
    void fim_do_fluxo() {
        if (fase_ == Fase::CorpoAteFechar) { fase_ = Fase::Fim; }
    }

    std::size_t recebidos() const { return recebidos_; }

    /// Quem recebe o corpo pediu para parar. É o `false` do `alimenta` que
    /// **não** é resposta malformada: a resposta podia estar boa.
    bool destino_parou() const { return destino_parou_; }

    /// Bytes de corpo anunciados, ou -1 quando o servidor não disse.
    long content_length() const { return content_length_; }

private:
    enum class Fase : std::uint8_t {
        Cabecalhos,
        CorpoAteFechar,    ///< sem Content-Length nem chunked
        CorpoContado,      ///< Content-Length
        PedacoTamanho,     ///< lendo a linha hexadecimal do pedaço
        PedacoDados,
        PedacoFimDeLinha,  ///< o CRLF depois dos dados do pedaço
        Fim,
    };

    bool processa_cabecalhos(AoReceberCorpo ao_receber, void* contexto);
    /// Entrega um trecho do corpo. `false` se quem recebe pediu para parar.
    bool entrega(AoReceberCorpo ao_receber, void* contexto,
                 const std::uint8_t* bytes, std::size_t n);

    Fase          fase_ = Fase::Cabecalhos;
    bool          destino_parou_ = false;
    char          cabecalhos_[kMaxCabecalhos] = {};
    std::size_t   n_cabecalhos_ = 0;
    std::uint32_t status_ = 0;
    long          content_length_ = -1;
    bool          por_pedacos_ = false;
    std::size_t   recebidos_ = 0;
    std::size_t   falta_no_pedaco_ = 0;
    char          linha_pedaco_[20] = {};
    std::size_t   n_linha_pedaco_ = 0;
};

}  // namespace coruja
