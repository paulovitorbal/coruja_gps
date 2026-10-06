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
#define MEM_SIZE                    4000
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
#define MEM_STATS                   0
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
