// Configuração do lwIP. Derivada do lwipopts comum dos pico-examples, com as
// mudanças que este projeto precisa comentadas uma a uma.
#pragma once

// Modo **poll**: sem sistema operacional e sem callbacks em interrupção. Tudo
// roda no laço principal, de dentro de `cyw43_arch_poll()`.
//
// A alternativa, `threadsafe_background`, chamaria o callback de recepção em
// contexto de IRQ — e é ali que este firmware calcula CRC-32 sobre 1460 bytes
// por pacote. Pôr isso numa interrupção é exatamente o tipo de decisão que
// depois aparece como travamento sem explicação.
#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
// 🔴 O HEAP DO lwIP, e e dele que sai a SESSAO TLS.
//
// Eram 4000 bytes, suficientes quando so havia pbufs aqui. Mas o
// `altcp_tls_create_config_client` aloca por `mem_calloc` -- o heap do lwIP,
// nao o alocador do mbedTLS -- e nele precisa caber a configuracao com os
// TRES certificados raiz JA INTERPRETADOS, um deles RSA de 4096 bits, mais o
// contexto de entropia.
//
// Nao cabia. O sintoma, medido em 2026-10-07:
//
//     [ERRO] http: sem memoria para a sessao TLS
//     [INFO] emprestimo: pico de uso: 0 B de 287984
//
// O zero e a prova: a arena emprestada da base nunca foi tocada, porque o
// mbedTLS nao chegou a alocar nada. A falta era antes, no lwIP.
//
// ⚠️ **A arena continua necessaria.** Ela atende as alocacoes INTERNAS do
// mbedTLS durante o handshake -- os numerosao da curva e do RSA --, que sao
// outra conta. Este heap atende a sessao; aquela, a matematica.
//
// ⚠️ Subir para 32 KiB NAO resolveu -- e a medida mostrou por que: o lwIP
// sequestrava o alocador do mbedTLS, entao os buffers de registro e os
// certificados vinham todos para ca. Com `MBEDTLS_PLATFORM_MEMORY` fora do
// `mbedtls_config.h`, eles passam ao heap do sistema e este volta a atender
// so o que sempre atendeu: pbufs e as estruturas pequenas do altcp.
//
// 8 KiB, e nao os 4000 originais, porque a configuracao de TLS e o estado por
// conexao ainda saem daqui. O `MEM_STATS` abaixo diz quanto de fato se usa.
#define MEM_SIZE                    8192
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0

#define TCP_WND                     (8 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETCONN                0
// Ligado para o firmware poder DIZER quanto do heap o TLS usou, em vez de
// alguem continuar escolhendo `MEM_SIZE` no olho. Custa alguns bytes de
// contadores.
#define MEM_STATS                   1
#define LWIP_STATS                  1
#define LWIP_STATS_DISPLAY          0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0

#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
// O DNS é obrigatório: as URLs da configuração podem ser nome, não IP.
#define LWIP_DNS                    1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

// O cliente HTTP do lwIP, usado pelo ClienteHttp.
#define LWIP_HTTPC_HAVE_FILE_IO     0

// ---------------------------------------------------------------------- TLS
//
// A camada `altcp` e uma indirecao sobre o TCP: o mesmo codigo de cliente
// fala com `tcp_*` cru ou com TLS por cima, conforme o alocador que recebe.
// E o que permite ter UM cliente HTTP em vez de dois.
#define LWIP_ALTCP                  1
#define LWIP_ALTCP_TLS              1
#define LWIP_ALTCP_TLS_MBEDTLS      1

// 🔴 ABORTAR o handshake quando o certificado nao valida.
//
// **O padrao do lwIP e `MBEDTLS_SSL_VERIFY_OPTIONAL`** (ver
// `altcp_tls_mbedtls_opts.h`), e `OPTIONAL` nao significa "verifica menos":
// significa que o mbedTLS verifica, guarda o resultado em
// `mbedtls_ssl_get_verify_result()` e **deixa a conexao seguir**. O lwIP
// nunca consulta esse resultado. Ou seja, com o padrao, um certificado
// forjado, vencido ou de outra autoridade e aceito em silencio -- e o TLS
// inteiro vira teatro.
//
// Este projeto embute tres raizes de proposito; recusar quem nao se encaixa
// nelas e o ponto. A linha abaixo e o que faz isso acontecer.
//
// O identificador vem do mbedTLS e nao esta definido aqui: macro so e
// expandida no ponto de uso, e la o `mbedtls/ssl.h` ja entrou.
#define ALTCP_MBEDTLS_AUTHMODE      MBEDTLS_SSL_VERIFY_REQUIRED
